
#include "config.h"
#include "cache/cache.h"
#include "vtim.h"
#include "vcc_example_if.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#define FREE_IF(ptr) if ((ptr)) { free((ptr)); (ptr) = NULL; }
#define SF MSG_NOSIGNAL
#define PORT 1377

const char *nfound_html = "HTTP/1.1 404\r\n"
						  "Content-Type: text/html; charset=utf-8\r\n"
						  "Content-Length: 86\r\n\r\n"
						  "<h3>404 Not Found</h3><br>"
						  "<a href=\"https://github.com/mrrva/libvmod-fs\">libvmod-fs</a>";
const char *errors_html = "HTTP/1.1 500\r\n"
						  "Content-Type: text/html; charset=utf-8\r\n"
						  "Content-Length: 92\r\n\r\n"
						  "<h3>Unable to read file</h3><br>"
						  "<a href=\"https://github.com/mrrva/libvmod-fs\">libvmod-fs</a>";
const char *connect_tmp = "HTTP/1.1 500\r\n"
						  "Content-Type: text/html; charset=utf-8\r\n"
						  "Content-Length: 113\r\n\r\n"
						  "<h3>Threads limit exceeded, please try later</h3><br>"
						  "<a href=\"https://github.com/mrrva/libvmod-fs\">libvmod-fs</a>";
const char *doc_headers = "HTTP/1.1 200\r\n"
						  "Content-Description: File Transfer\r\n"
						  "Content-Type: application/octet-stream\r\n"
						  "Content-Disposition: attachment; filename=%s\r\n"
						  "Expires: 0\r\n"
						  "Cache-Control: must-revalidate\r\n"
						  "Pragma: public\r\n"
						  "Content-Length: %ld\r\n\r\n";
void *server_thread(void *);
bool work_thread_ = true;
char *path_ = NULL, *regex_ = NULL;
pthread_t thread_;
int max_threads_;
atomic_int threads_ = 0;

int v_matchproto_(vmod_event_f) vmod_event_function(VRT_CTX, struct vmod_priv *priv,
													enum vcl_event_e event) {
	(void) priv;
	(void) ctx;

	void *ret;
	switch (event) {
	case VCL_EVENT_LOAD:
		work_thread_ = pthread_create(&thread_, NULL, server_thread, NULL) == 0;
		break;

	case VCL_EVENT_DISCARD:
		work_thread_ = false;
		pthread_join(thread_, &ret);
		FREE_IF(path_);
		FREE_IF(regex_);
		break;

	case VCL_EVENT_COLD:
	case VCL_EVENT_WARM:
	default:
		break;
	}

	return 0;
}

VCL_VOID vmod_init(VRT_CTX, VCL_STRING path, VCL_STRING url, VCL_INT max_connections) {
	(void) ctx;
	FREE_IF(regex_);
	FREE_IF(path_);

	const int len = strlen(path);
	max_threads_ = max_connections ? max_connections : 100;
	path_ = len > 1 && path[len - 1] == '/' ? strdup(path) : NULL;

	regex_ = (char *)malloc(strlen(url) + 20);
	if (regex_) {
		sprintf(regex_, "GET %s%s HTTP*", url, "%100s");
	}
}

VCL_STRING vmod_dir(VRT_CTX) {
	(void) ctx;
	return path_ ? path_ : "NULL";
}

VCL_BOOL vmod_work(VRT_CTX) {
	(void) ctx;
	return work_thread_;
}

void close_sock(int desc) {
	if (desc > -1) {
		shutdown(desc, SHUT_RDWR);
		close(desc);
	}
}

void *client_thread(void *args) {
	int sock = *((int *)args), buff_size = 300, ret;
	char *full = NULL;

	if (!(full = (char *)malloc(buff_size + 1))) {
		goto _client_thread_exit;
	}

	memset(full, 0, buff_size + 1);
	bool flag = false;
	char r_tmp[101];
	int  r_num = 0;

	while (!flag) {
		memset(r_tmp, 0, 101);
		ret = recv(sock, r_tmp, 100, 0);

		if (ret <= 0 || r_num + ret > buff_size) {
			break;
		}

		strncat(full, r_tmp, ret);
		r_num += ret;

		for (int i = 0; i < r_num; i++) {
			if (full[i] == '\r' || full[i] == '\n') {
				flag = true;
				break;
			}
		}
	}

	if (!path_ || !regex_) {
		send(sock, errors_html, strlen(errors_html), SF);
		goto _client_thread_exit;
	}

	memset(r_tmp, 0, 101);
	sscanf(full, regex_, r_tmp);

	if (strlen(r_tmp) < 4) {
		send(sock, nfound_html, strlen(nfound_html), SF);
		goto _client_thread_exit;
	}

	char *full_path = (char *)malloc(strlen(path_) + strlen(r_tmp) + 1);
	if (!full_path) {
		goto _client_thread_exit;
	}

	strcpy(full_path, path_);
	strcat(full_path, r_tmp);
	FILE *fp = fopen(full_path, "rb+");

	free(full_path);
	free(full);

	if (!fp) {
		send(sock, nfound_html, strlen(nfound_html), SF);
		goto _client_thread_exit;
	}

	fseek(fp, 0L, SEEK_END);
	size_t size = ftell(fp);
	rewind(fp);

	full = (char *)malloc(strlen(doc_headers) + strlen(r_tmp) + 15);
	if (!full || size > 10000000000) {
		goto _fp_client_thread_exit;
	}

	sprintf(full, doc_headers, r_tmp, size);
	send(sock, full, strlen(full), SF);
	flag = false;

	while ((ret = fread(r_tmp, sizeof(char), 100, fp))) {
		int step = 0, sret;
		
		while ((sret = send(sock, r_tmp + step, ret, SF)) != ret) {
			if (sret <= 0) {
				flag = true;
				break;
			}

			step += sret;
			ret  -= sret;
		}
		if (flag) {
			break;
		}
	}

_fp_client_thread_exit:
	fclose(fp);

_client_thread_exit:
	close_sock(sock);
	FREE_IF(full);
	threads_--;

	return NULL;
}

void *server_thread(void *args) {
	int sock = socket(AF_INET, SOCK_STREAM, 0), bs;
	struct sockaddr_in sddr;
	struct sockaddr *ptr = (struct sockaddr *)&sddr;
	socklen_t size = sizeof(struct sockaddr_in);

	if (sock < 0) {
		goto _server_thread_exit;
	}

	sddr.sin_addr.s_addr = INADDR_ANY;
	sddr.sin_port = htons(PORT);
	sddr.sin_family = AF_INET;

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
		goto _server_thread_exit;
	}
	if (bind(sock, ptr, size) != 0 || listen(sock, 5) != 0) {
		goto _server_thread_exit;
	}

	struct sockaddr_in client;
	struct sockaddr *cptr = (struct sockaddr *)&client;

	while (work_thread_) {
		if ((bs = accept(sock, cptr, &size)) >= 0) {
			if (threads_ >= max_threads_) {
				send(bs, connect_tmp, strlen(connect_tmp), SF);
				close_sock(bs);
				continue;
			}

			pthread_t handle;
			int ret = pthread_create(&handle, NULL, client_thread, (void *)&bs);
			threads_++;

			if (ret + pthread_detach(handle) != 0) {
				work_thread_ = false;
				close_sock(bs);
			}
		}
	}

_server_thread_exit:
	work_thread_ = false;
	close_sock(sock);

	return NULL;
}
