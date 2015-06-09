/*
	libusbmuxd - client library to talk to usbmuxd

Copyright (C) 2009-2010	Nikias Bassen <nikias@gmx.li>
Copyright (C) 2009	Paul Sladen <libiphone@paul.sladen.org>
Copyright (C) 2009	Martin Szulecki <opensuse@sukimashita.com>

This library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 2.1 of the
License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef WIN32

#include <winsock2.h>
#include <windows.h>
#define sleep(x) Sleep(x*1000)

#undef EPROTO
#define EPROTO 71
#undef EBADMSG
#define EBADMSG 77

#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#endif

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define USBMUXD_DIRNAME "/var/run"
#define USBMUXD_SOCKET_NAME "usbmuxd"
#endif /* HAVE_INOTIFY */

//#include <unistd.h>
#include <signal.h>

#ifdef HAVE_PLIST
#include <plist/plist.h>
#define PLIST_BUNDLE_ID "com.marcansoft.usbmuxd"
#define PLIST_CLIENT_VERSION_STRING "usbmuxd built for freedom"
#define PLIST_PROGNAME "libusbmuxd"
#endif

// usbmuxd public interface
#include "usbmuxd.h"
// usbmuxd protocol 
#include "usbmuxd-proto.h"
// socket utility functions
#include "sock_stuff.h"
// misc utility functions
#include "utils.h"

#ifndef DEBUG_DATA
# define DEBUG_DATA 0
#endif

#ifndef DEBUG_DATA_HEX
# define DEBUG_DATA_HEX 1
#endif

static int libusbmuxd_debug = DEBUG_DATA;
#ifndef __func__
# define __func__ __FUNCTION__
#endif
#define DEBUG(x, y, ...) if (x <= libusbmuxd_debug) fprintf(stderr, (y), __VA_ARGS__); fflush(stderr);

#if DEBUG_DATA

#define DEBUG_DATA_HEX_WIDTH 30

static char tohex(int x)
{
    static char* hexchars = "0123456789ABCDEF";
    return hexchars[x];
}

static unsigned int fromhex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    else if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return 0;
}

static char *hex2strn(const char *data, const int len)
{
	if (len == 0) {
		return NULL;
	}
	int i, j;
	int result_len = len/2 + 1;

	char *result = (char *)malloc((result_len + 1) * sizeof(char));

	for (i=0, j=0; i<len; i+=2) {
		result[j++] = fromhex(data[i]) << 4 | fromhex(data[i+1]);
	}
	result[j] = 0x00;
	return result;
}

static char *str2hexn(const char *data, const int len)
{
	if (len == 0) {
		return NULL;
	}
	int i, j;
	int new_lines_count = len / DEBUG_DATA_HEX_WIDTH;
	int result_len = len * 2 + new_lines_count;
	char *result = (char *)malloc((result_len + 1) * sizeof(char));

	for (i=0, j=0; i<len; i++) {
		result[j++] = tohex((data[i]&0xf0)>>4);
		result[j++] = tohex(data[i]&0x0f);
		if (i > 0 && i % DEBUG_DATA_HEX_WIDTH == 0) {
			result[j++] = 0xa;
		}
	}
	result[j] = 0x00;
	return result;
}

static char *hex2str(const char *data)
{
	return hex2strn(data, strlen(data));
}

static char *str2hex(const char *data)
{
	return str2hexn(data, strlen(data));
}

#endif

static struct collection devices;
static usbmuxd_event_cb_t event_cb = NULL;
#ifdef WIN32
HANDLE devmon = NULL;
CRITICAL_SECTION mutex;
static int mutex_initialized = 0;
#define LOCK if (!mutex_initialized) { InitializeCriticalSection(&mutex); mutex_initialized = 1; } EnterCriticalSection(&mutex);
#define UNLOCK LeaveCriticalSection(&mutex);
#else
pthread_t devmon;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&mutex)
#define UNLOCK pthread_mutex_unlock(&mutex)	
#endif
static int listenfd = -1;

static int use_tag = 0;
static int proto_version = 0;

/**
 * Finds a device info record by its handle.
 * if the record is not found, NULL is returned.
 */
static usbmuxd_device_info_t *devices_find(int handle)
{
	FOREACH(usbmuxd_device_info_t *dev, &devices, usbmuxd_device_info_t *) {
		if (dev && dev->handle == handle) {
			return dev;
		}
	} ENDFOREACH
	return NULL;
}

/**
 * Creates a socket connection to usbmuxd.
 * For Mac/Linux it is a unix domain socket,
 * for Windows it is a tcp socket.
 */
static int connect_usbmuxd_socket()
{
#if defined(WIN32) || defined(__CYGWIN__)
	return connect_socket("127.0.0.1", USBMUXD_SOCKET_PORT);
#else
	return connect_unix_socket(USBMUXD_SOCKET_FILE);
#endif
}

static int receive_packet(int sfd, struct usbmuxd_header *header, void **payload, int timeout)
{
	int recv_len;
	struct usbmuxd_header hdr;
	char *payload_loc = NULL;

	header->length = 0;
	header->version = 0;
	header->message = 0;
	header->tag = 0;

	recv_len = recv_buf_timeout(sfd, &hdr, sizeof(hdr), 0, timeout);
	if (recv_len < 0) {
		return recv_len;
	} else if (recv_len < sizeof(hdr)) {
		return recv_len;
	}

	uint32_t payload_size = hdr.length - sizeof(hdr);
	if (payload_size > 0) {
		payload_loc = (char*)malloc(payload_size);
		if (recv_buf_timeout(sfd, payload_loc, payload_size, 0, 5000) != payload_size) {
			DEBUG(1, "%s: Error receiving payload of size %d\n", __FUNCTION__, payload_size);
			free(payload_loc);
			return -EBADMSG;
		}
	}

#ifdef HAVE_PLIST
	if (hdr.message == MESSAGE_PLIST) {
		char *message = NULL;
		plist_t plist = NULL;
		plist_from_xml(payload_loc, payload_size, &plist);
		free(payload_loc);

		if (!plist) {
			DEBUG(1, "%s: Error getting plist from payload!\n", __func__);
			return -EBADMSG;
		}

		plist_t node = plist_dict_get_item(plist, "MessageType");
		if (plist_get_node_type(node) != PLIST_STRING) {
			DEBUG(1, "%s: Error getting message type from plist!\n", __func__);
			free(plist);
			return -EBADMSG;
		}
		
		plist_get_string_val(node, &message);
		if (message) {
			uint64_t val = 0;
			if (strcmp(message, "Result") == 0) {
				/* result message */
				uint32_t dwval = 0;
				plist_t n = plist_dict_get_item(plist, "Number");
				plist_get_uint_val(n, &val);
				*payload = malloc(sizeof(uint32_t));
				dwval = val;
				memcpy(*payload, &dwval, sizeof(dwval));
				hdr.length = sizeof(hdr) + sizeof(dwval);
				hdr.message = MESSAGE_RESULT;
			} else if (strcmp(message, "Attached") == 0) {
				/* device add message */
				struct usbmuxd_device_record *dev = NULL;
				plist_t props = plist_dict_get_item(plist, "Properties");
				if (!props) {
					DEBUG(1, "%s: Could not get properties for message '%s' from plist!\n", __func__, message);
					plist_free(plist);
					return -EBADMSG;
				}
				dev = (struct usbmuxd_device_record*)malloc(sizeof(struct usbmuxd_device_record));
				memset(dev, 0, sizeof(struct usbmuxd_device_record));

				plist_t n = plist_dict_get_item(props, "DeviceID");
				plist_get_uint_val(n, &val);
				dev->device_id = (uint32_t)val;

				n = plist_dict_get_item(props, "ProductID");
				plist_get_uint_val(n, &val);
				dev->product_id = (uint32_t)val;

				n = plist_dict_get_item(props, "SerialNumber");
				char *strval = NULL;
				plist_get_string_val(n, &strval);
				if (strval) {
					strncpy(dev->serial_number, strval, 255);
					free(strval);
				}
				n = plist_dict_get_item(props, "LocationID");
				plist_get_uint_val(n, &val);
				dev->location = (uint32_t)val;
				*payload = (void*)dev;
				hdr.length = sizeof(hdr) + sizeof(struct usbmuxd_device_record);
				hdr.message = MESSAGE_DEVICE_ADD;
			} else if (strcmp(message, "Detached") == 0) {
				/* device remove message */
				uint32_t dwval = 0;
				plist_t n = plist_dict_get_item(plist, "DeviceID");
				if (n) {
					plist_get_uint_val(n, &val);
					*payload = malloc(sizeof(uint32_t));
					dwval = val;
					memcpy(*payload, &dwval, sizeof(dwval));
					hdr.length = sizeof(hdr) + sizeof(dwval);
					hdr.message = MESSAGE_DEVICE_REMOVE;
				}
			} else {
				DEBUG(1, "%s: Unexpected message '%s' in plist!\n", __func__, message);
				plist_free(plist);
				return -EBADMSG;
			}
		}
		plist_free(plist);
	} else
#endif
	{
		*payload = payload_loc;
	}

	memcpy(header, &hdr, sizeof(hdr));

	return hdr.length;
}

/**
 * Retrieves the result code to a previously sent request.
 */
static int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t * result)
{
	struct usbmuxd_header hdr;
	int recv_len;
	uint32_t *res = NULL;

	if (!result) {
		return -EINVAL;
	}
	*result = -1;

	if ((recv_len = receive_packet(sfd, &hdr, (void**)&res, 5000)) < 0) {
		DEBUG(1, "%s: Error receiving packet: %d\n", __FUNCTION__, errno);
		if (res)
			free(res);
		return -errno;
	}
	if (recv_len < sizeof(hdr)) {
		DEBUG(1, "%s: Received packet is too small!\n", __FUNCTION__);
		if (res)
			free(res);
		return -EPROTO;
	}

	if (hdr.message == MESSAGE_RESULT) {
		int ret = 0;
		if (res && (hdr.tag == tag)) {
			memcpy(result, res, sizeof(uint32_t));
			ret = 1;
		}
		if (res)
			free(res);
		return ret;
	}
	DEBUG(1, "%s: Unexpected message of type %d received!\n", __FUNCTION__, hdr.message);
	if (res)
		free(res);
	return -EPROTO;
}

static int send_packet(int sfd, uint32_t message, uint32_t tag, void *payload, uint32_t payload_size)
{
	struct usbmuxd_header header;

	char *hz = (char *)payload;

	header.length = sizeof(struct usbmuxd_header);
	header.version = proto_version;
	header.message = message;
	header.tag = tag;
	if (payload && (payload_size > 0)) {
		header.length += payload_size;
	}
	int sent = send_buf(sfd, &header, sizeof(header));
	if (sent != sizeof(header)) {
		DEBUG(1, "%s: ERROR: could not send packet header\n", __FUNCTION__);
		return -1;
	}
	if (payload && (payload_size > 0)) {
		sent += send_buf(sfd, payload, payload_size);
	}
	if (sent != (int)header.length) {
		DEBUG(1, "%s: ERROR: could not send whole packet\n", __FUNCTION__);
		close_socket(sfd);
		return -1;
	}
	return sent;
}

static int send_listen_packet(int sfd, uint32_t tag)
{
	int res = 0;
#ifdef HAVE_PLIST
	if (proto_version == 1) {
		/* plist packet */
		char *payload = NULL;
		uint32_t payload_size = 0;
		plist_t plist;

		/* construct message plist */
		plist = plist_new_dict();
		plist_dict_insert_item(plist, "BundleID", plist_new_string(PLIST_BUNDLE_ID));
		plist_dict_insert_item(plist, "ClientVersionString", plist_new_string(PLIST_CLIENT_VERSION_STRING));
		plist_dict_insert_item(plist, "MessageType", plist_new_string("Listen"));
		plist_dict_insert_item(plist, "ProgName", plist_new_string(PLIST_PROGNAME));
		plist_to_xml(plist, &payload, &payload_size);
		plist_free(plist);

		res = send_packet(sfd, MESSAGE_PLIST, tag, payload, payload_size);
		free(payload);
	} else
#endif
	{
		/* binary packet */
		res = send_packet(sfd, MESSAGE_LISTEN, tag, NULL, 0);
	}
	return res;
}

static int send_connect_packet(int sfd, uint32_t tag, uint32_t device_id, uint16_t port)
{
	int res = 0;
#ifdef HAVE_PLIST
	if (proto_version == 1) {
		/* plist packet */
		char *payload = NULL;
		uint32_t payload_size = 0;
		plist_t plist;

		/* construct message plist */
		plist = plist_new_dict();
		plist_dict_insert_item(plist, "BundleID", plist_new_string(PLIST_BUNDLE_ID));
		plist_dict_insert_item(plist, "ClientVersionString", plist_new_string(PLIST_CLIENT_VERSION_STRING));
		plist_dict_insert_item(plist, "MessageType", plist_new_string("Connect"));
		plist_dict_insert_item(plist, "DeviceID", plist_new_uint(device_id));
		plist_dict_insert_item(plist, "PortNumber", plist_new_uint(htons(port)));
		plist_dict_insert_item(plist, "ProgName", plist_new_string(PLIST_PROGNAME));
		plist_to_xml(plist, &payload, &payload_size);
		plist_free(plist);

		res = send_packet(sfd, MESSAGE_PLIST, tag, (void*)payload, payload_size);
		free(payload);
	} else
#endif
	{
		/* binary packet */
		struct {
			uint32_t device_id;
			uint16_t port;
			uint16_t reserved;
		} conninfo;

		conninfo.device_id = device_id;
		conninfo.port = htons(port);
		conninfo.reserved = 0;

		res = send_packet(sfd, MESSAGE_CONNECT, tag, &conninfo, sizeof(conninfo));
	}
	return res;
}

/**
 * Generates an event, i.e. calls the callback function.
 * A reference to a populated usbmuxd_event_t with information about the event
 * and the corresponding device will be passed to the callback function.
 */
static void generate_event(usbmuxd_event_cb_t callback, const usbmuxd_device_info_t *dev, enum usbmuxd_event_type event, void *user_data)
{
	usbmuxd_event_t ev;

	if (!callback || !dev) {
		return;
	}

	ev.event = event;
	memcpy(&ev.device, dev, sizeof(usbmuxd_device_info_t));

	callback(&ev, user_data);
}

static int usbmuxd_listen_poll()
{
	int sfd;

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		while (event_cb) {
			if ((sfd = connect_usbmuxd_socket()) > 0) {
				break;
			}
			sleep(1);
		}
	}

	return sfd;
}

#ifdef HAVE_INOTIFY
static int use_inotify = 1;

static int usbmuxd_listen_inotify()
{
	int inot_fd;
	int watch_d;
	int sfd;

	if (!use_inotify) {
		return -2;
	}

	sfd = connect_usbmuxd_socket();
	if (sfd >= 0)
		return sfd;

	sfd = -1;
	inot_fd = inotify_init ();
	if (inot_fd < 0) {
		DEBUG(1, "%s: Failed to setup inotify\n", __func__);
		return -2;
	}

	/* inotify is setup, listen for events that concern us */
	watch_d = inotify_add_watch (inot_fd, USBMUXD_DIRNAME, IN_CREATE);
	if (watch_d < 0) {
		DEBUG(1, "%s: Failed to setup watch descriptor for socket dir\n", __func__);
		close (inot_fd);
		return -2;
	}

	while (1) {
		ssize_t len, i;
		char buff[EVENT_BUF_LEN] = {0};

		i = 0;
		len = read (inot_fd, buff, EVENT_BUF_LEN -1);
		if (len < 0)
			goto end;
		while (i < len) {
			struct inotify_event *pevent = (struct inotify_event *) & buff[i];

			/* check that it's ours */
			if (pevent->mask & IN_CREATE &&
			    pevent->len &&
			    pevent->name != NULL &&
			    strcmp(pevent->name, USBMUXD_SOCKET_NAME) == 0) {
				sfd = connect_usbmuxd_socket ();
				goto end;
			}
			i += EVENT_SIZE + pevent->len;
		}
	}

end:
	inotify_rm_watch(inot_fd, watch_d);
	close(inot_fd);

	return sfd;
}
#endif /* HAVE_INOTIFY */

/**
 * Tries to connect to usbmuxd and wait if it is not running.
 */
static int usbmuxd_listen()
{
	int sfd;
	uint32_t res = -1;

#ifdef HAVE_PLIST
retry:
#endif

#ifdef HAVE_INOTIFY
	sfd = usbmuxd_listen_inotify();
	if (sfd == -2)
		sfd = usbmuxd_listen_poll();
#else
	sfd = usbmuxd_listen_poll();
#endif

	if (sfd < 0) {
		DEBUG(1, "%s: ERROR: usbmuxd was supposed to be running here...\n", __FUNCTION__);
		return sfd;
	}

	use_tag++;
	LOCK;
	if (send_listen_packet(sfd, use_tag) <= 0) {
		UNLOCK;
		DEBUG(1, "%s: ERROR: could not send listen packet\n", __FUNCTION__);
		close_socket(sfd);
		return -1;
	}
	if (usbmuxd_get_result(sfd, use_tag, &res) && (res != 0)) {
		UNLOCK;
		close_socket(sfd);
#ifdef HAVE_PLIST
		if ((res == RESULT_BADVERSION) && (proto_version != 1)) {
			proto_version = 1;
			goto retry;
		}
#endif
		DEBUG(1, "%s: ERROR: did not get OK but %d\n", __FUNCTION__, res);
		return -1;
	}
	UNLOCK;

	return sfd;
}

/**
 * Waits for an event to occur, i.e. a packet coming from usbmuxd.
 * Calls generate_event to pass the event via callback to the client program.
 */
int get_next_event(int sfd, usbmuxd_event_cb_t callback, void *user_data)
{
	struct usbmuxd_header hdr;
	void *payload = NULL;

	/* block until we receive something */
	if (receive_packet(sfd, &hdr, &payload, 0) < 0) {
		// when then usbmuxd connection fails,
		// generate remove events for every device that
		// is still present so applications know about it
		FOREACH(usbmuxd_device_info_t *dev, &devices, usbmuxd_device_info_t *) {
			generate_event(callback, dev, UE_DEVICE_REMOVE, user_data);
			collection_remove(&devices, dev);
			free(dev);
		} ENDFOREACH
		return -EIO;
	}

	if ((hdr.length > sizeof(hdr)) && !payload) {
		DEBUG(1, "%s: Invalid packet received, payload is missing!\n", __FUNCTION__);
		return -EBADMSG;
	}

	if (hdr.message == MESSAGE_DEVICE_ADD) {
		struct usbmuxd_device_record *dev = (struct usbmuxd_device_record *) payload;
		usbmuxd_device_info_t *devinfo = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t));
		if (!devinfo) {
			DEBUG(1, "%s: Out of memory!\n", __FUNCTION__);
			free(payload);
			return -1;
		}

		devinfo->handle = dev->device_id;
		devinfo->product_id = dev->product_id;
		memset(devinfo->udid, '\0', sizeof(devinfo->udid));
		memcpy(devinfo->udid, dev->serial_number, sizeof(devinfo->udid));

		if (_stricmp(devinfo->udid, "ffffffffffffffffffffffffffffffffffffffff") == 0) {
			sprintf(devinfo->udid + 32, "%08x", devinfo->handle);
		}

		collection_add(&devices, devinfo);
		generate_event(callback, devinfo, UE_DEVICE_ADD, user_data);
	} else if (hdr.message == MESSAGE_DEVICE_REMOVE) {
		uint32_t handle;
		usbmuxd_device_info_t *devinfo;

		memcpy(&handle, payload, sizeof(uint32_t));

		devinfo = devices_find(handle);
		if (!devinfo) {
			DEBUG(1, "%s: WARNING: got device remove message for handle %d, but couldn't find the corresponding handle in the device list. This event will be ignored.\n", __FUNCTION__, handle);
		} else {
			generate_event(callback, devinfo, UE_DEVICE_REMOVE, user_data);
			collection_remove(&devices, devinfo);
			free(devinfo);
		}
	} else if (hdr.length > 0) {
		DEBUG(1, "%s: Unexpected message type %d length %d received!\n", __FUNCTION__, hdr.message, hdr.length);
	}
	if (payload) {
		free(payload);
	}
	return 0;
}

static void device_monitor_cleanup(void* data)
{
	collection_free(&devices);

	close_socket(listenfd);
	listenfd = -1;
}

/**
 * Device Monitor thread function.
 *
 * This function sets up a connection to usbmuxd
 */
static void *device_monitor(void *data)
{
	collection_init(&devices);

#ifndef WIN32
	pthread_cleanup_push(device_monitor_cleanup, NULL);
#endif
	while (event_cb) {

		listenfd = usbmuxd_listen();
		if (listenfd < 0) {
			continue;
		}

		while (event_cb) {
			int res = get_next_event(listenfd, event_cb, data);
			if (res < 0) {
			    break;
			}
		}
	}

#ifndef WIN32
	pthread_cleanup_pop(1);
#else
	device_monitor_cleanup(NULL);
#endif
	return NULL;
}

int usbmuxd_subscribe(usbmuxd_event_cb_t callback, void *user_data)
{
	int res;

	if (!callback) {
		return -EINVAL;
	}
	event_cb = callback;

#ifdef WIN32
	res = 0;
	devmon = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)device_monitor, user_data, 0, NULL);
	if (devmon == NULL) {
		res = GetLastError();
	}
#else
	res = pthread_create(&devmon, NULL, device_monitor, user_data);
#endif
	if (res != 0) {
		DEBUG(1, "%s: ERROR: Could not start device watcher thread!\n", __FUNCTION__);
		return res;
	}
	return 0;
}

int usbmuxd_unsubscribe()
{
	event_cb = NULL;

	shutdown_socket(listenfd, SHUT_RDWR);

#ifdef WIN32
	if (devmon != NULL) {
		WaitForSingleObject(devmon, INFINITE);
	}
#else
	if (pthread_kill(devmon, 0) == 0) {
		pthread_cancel(devmon);
		pthread_join(devmon, NULL);
	}
#endif

	return 0;
}

int usbmuxd_get_device_list(usbmuxd_device_info_t **device_list)
{
	int sfd;
	int listen_success = 0;
	uint32_t res;
	struct collection tmpdevs;
	usbmuxd_device_info_t *newlist = NULL;
	struct usbmuxd_header hdr;
	struct usbmuxd_device_record *dev;
	int dev_cnt = 0;
	void *payload = NULL;

	*device_list = NULL;

#ifdef HAVE_PLIST
retry:
#endif
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: error opening socket!\n", __FUNCTION__);
		return sfd;
	}

	use_tag++;
	LOCK;
	if (send_listen_packet(sfd, use_tag) > 0) {
		res = -1;
		// get response
		if (usbmuxd_get_result(sfd, use_tag, &res) && (res == 0)) {
			listen_success = 1;
		} else {
			UNLOCK;
			close_socket(sfd);
#ifdef HAVE_PLIST
			if ((res == RESULT_BADVERSION) && (proto_version != 1)) {
				proto_version = 1;
				goto retry;
			}
#endif
			DEBUG(1, "%s: Did not get response to scan request (with result=0)...\n", __FUNCTION__);
			return res;
		}
	}

	if (!listen_success) {
		UNLOCK;
		DEBUG(1, "%s: Could not send listen request!\n", __FUNCTION__);
		return -1;
	}

	collection_init(&tmpdevs);

	// receive device list
	while (1) {
		if (receive_packet(sfd, &hdr, &payload, 1000) > 0) {
			if (hdr.message == MESSAGE_DEVICE_ADD) {
				dev = (struct usbmuxd_device_record *) payload;
				usbmuxd_device_info_t *devinfo = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t));
				if (!devinfo) {
					UNLOCK;
					DEBUG(1, "%s: Out of memory!\n", __FUNCTION__);
					free(payload);
					return -1;
				}

				devinfo->handle = dev->device_id;
				devinfo->product_id = dev->product_id;
				memset(devinfo->udid, '\0', sizeof(devinfo->udid));
				memcpy(devinfo->udid, dev->serial_number, sizeof(devinfo->udid));

				if (_stricmp(devinfo->udid, "ffffffffffffffffffffffffffffffffffffffff") == 0) {
					sprintf(devinfo->udid + 32, "%08x", devinfo->handle);
				}

				collection_add(&tmpdevs, devinfo);

			} else if (hdr.message == MESSAGE_DEVICE_REMOVE) {
				uint32_t handle;
				usbmuxd_device_info_t *devinfo = NULL;

				memcpy(&handle, payload, sizeof(uint32_t));

				FOREACH(usbmuxd_device_info_t *di, &tmpdevs, usbmuxd_device_info_t *) {
					if (di && di->handle == handle) {
						devinfo = di;
						break;
					}
				} ENDFOREACH
				if (devinfo) {
					collection_remove(&tmpdevs, devinfo);
					free(devinfo);
				}
			} else {
				DEBUG(1, "%s: Unexpected message %d\n", __FUNCTION__, hdr.message);
			}
			if (payload)
				free(payload);
		} else {
			// we _should_ have all of them now.
			// or perhaps an error occured.
			break;
		}
	}
	UNLOCK;

	// explicitly close connection
	close_socket(sfd);

	// create copy of device info entries from collection
	newlist = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t) * (collection_count(&tmpdevs) + 1));
	dev_cnt = 0;
	FOREACH(usbmuxd_device_info_t *di, &tmpdevs, usbmuxd_device_info_t *) {
		if (di) {
			memcpy(&newlist[dev_cnt], di, sizeof(usbmuxd_device_info_t));
			free(di);
			dev_cnt++;
		}
	} ENDFOREACH
	collection_free(&tmpdevs);

	memset(&newlist[dev_cnt], 0, sizeof(usbmuxd_device_info_t));
	*device_list = newlist;

	return dev_cnt;
}

int usbmuxd_device_list_free(usbmuxd_device_info_t **device_list)
{
	if (device_list) {
		free(*device_list);
	}
	return 0;
}

int usbmuxd_get_device_by_udid(const char *udid, usbmuxd_device_info_t *device)
{
	usbmuxd_device_info_t *dev_list = NULL;

	if (!device) {
		return -EINVAL;
	}
	if (usbmuxd_get_device_list(&dev_list) < 0) {
		return -ENODEV;
	}

	int i;
	int result = 0;
	for (i = 0; dev_list[i].handle > 0; i++) {
	 	if (!udid) {
			device->handle = dev_list[i].handle;
			device->product_id = dev_list[i].product_id;
			strcpy(device->udid, dev_list[i].udid);
			result = 1;
			break;
		}
		if (!strcmp(udid, dev_list[i].udid)) {
			device->handle = dev_list[i].handle;
			device->product_id = dev_list[i].product_id;
			strcpy(device->udid, dev_list[i].udid);
			result = 1;
			break;
		}
	}

	free(dev_list);

	return result;
}

int usbmuxd_connect(const int handle, const unsigned short port)
{
	int sfd;
	int connected = 0;
	uint32_t res = -1;

#ifdef HAVE_PLIST
retry:
#endif
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n",
				__FUNCTION__, strerror(errno));
		return sfd;
	}

	use_tag++;
	if (send_connect_packet(sfd, use_tag, (uint32_t)handle, (uint16_t)port) <= 0) {
		DEBUG(1, "%s: Error sending connect message!\n", __FUNCTION__);
	} else {
		// read ACK
		DEBUG(2, "%s: Reading connect result...\n", __FUNCTION__);
		if (usbmuxd_get_result(sfd, use_tag, &res)) {
			if (res == 0) {
				DEBUG(2, "%s: Connect success!\n", __FUNCTION__);
				connected = 1;
			} else {
#ifdef HAVE_PLIST
				if ((res == RESULT_BADVERSION) && (proto_version == 0)) {
					proto_version = 1;
					close_socket(sfd);
					goto retry;
				}
#endif
				DEBUG(1, "%s: Connect failed, Error code=%d\n", __FUNCTION__, res);
			}
		}
	}

	if (connected) {
		return sfd;
	}

	close_socket(sfd);

	return -1;
}

int usbmuxd_disconnect(int sfd)
{
	return close_socket(sfd);
}

int usbmuxd_send(int sfd, const char *data, uint32_t len, uint32_t *sent_bytes)
{
	int num_sent;

	if (sfd < 0) {
		return -EINVAL;
	}
	
#if DEBUG_DATA
	if (len > 0) {
		char *tmp = NULL;
		if (DEBUG_DATA_HEX && *data != '<') {
			tmp = str2hexn(data, len);
		} else {
			// plist here, show as text
			tmp = (char *)malloc((len+1) * sizeof(char));
			strncpy(tmp, data, len * sizeof(char));
			tmp[len] = 0x00;
		}

		DEBUG(1, "\n======================\nsending: %d bytes:\n%s\n======================\n\n", len, tmp);
		free(tmp);
	}

#endif

	num_sent = send(sfd, (const char *)(void*)data, len, 0);
	if (num_sent < 0) {
		*sent_bytes = 0;
		DEBUG(1, "%s: Error %d when sending: %s\n", __FUNCTION__, num_sent, strerror(errno));
		return num_sent;
	} else if ((uint32_t)num_sent < len) {
		DEBUG(1, "%s: Warning: Did not send enough (only %d of %d)\n", __FUNCTION__, num_sent, len);
	}

	*sent_bytes = num_sent;

	return 0;
}

int usbmuxd_recv_timeout(int sfd, char *data, uint32_t len, uint32_t *recv_bytes, unsigned int timeout)
{
	int num_recv = recv_buf_timeout(sfd, (void*)data, len, 0, timeout);

	if (num_recv < 0) {
		*recv_bytes = 0;
		return num_recv;
	}

#if DEBUG_DATA
	if (num_recv > 0) {
		char *tmp = NULL;
		if (DEBUG_DATA_HEX && *data != '<') {
			tmp = str2hexn(data, num_recv);
		} else {
			// plist here, show as text
			tmp = (char *)malloc((num_recv+1) * sizeof(char));
			strncpy(tmp, data, num_recv * sizeof(char));
			tmp[num_recv] = 0x00;
		}
		DEBUG(1, "\n======================\nreceived: %d bytes\n%s\n======================\n\n", len, tmp);
		free(tmp);
	}
#endif

	*recv_bytes = num_recv;

	return 0;
}

int usbmuxd_recv(int sfd, char *data, uint32_t len, uint32_t *recv_bytes)
{
	return usbmuxd_recv_timeout(sfd, data, len, recv_bytes, 5000);
}

void libusbmuxd_set_use_inotify(int set)
{
#ifdef HAVE_INOTIFY
	use_inotify = set;
#endif
	return;
}

void libusbmuxd_set_debug_level(int level)
{
	libusbmuxd_debug = level;
	sock_stuff_set_verbose(level);
}
