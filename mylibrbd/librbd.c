// Author: Siddhesh Rane <rane.si@northeastern.edu>

#include "librbd.h"
#include "../bs3/libbs3.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

void rbd_version(int *major, int *minor, int *extra) {
  *major = LIBRBD_VER_MAJOR;
  *minor = LIBRBD_VER_MINOR;
  *extra = LIBRBD_VER_EXTRA;
}

// Qemu needs a block size of atleast 4096 (2^12) and bs3 is by default
// configured to use the same
const int BLOCK_SIZE_ORDER = 12;

// The size in bytes of the entire disk
const size_t DISK_SIZE = 1024 * 1024 * 1024; // 1GB

// Always successfully create an image
int rbd_create(rados_ioctx_t io, const char *name, uint64_t size, int *order) {
  *order = BLOCK_SIZE_ORDER;
  // TODO: call open and then close; if any fails, then return fail
  return 0; // success
}

// Always successfully remove
int rbd_remove(rados_ioctx_t io, const char *name) {
  return 0; // success
}

int rbd_open(rados_ioctx_t io, const char *name, rbd_image_t *image,
             const char *snap_name) {
  int ret = bs3Open();
  if (ret < 0)
    return ret;
  *image = (void *)0xCAFEBABE;
  return ret;
}

int rbd_close(rbd_image_t image) {
  // Call bs3Close in go code
  return bs3Close();
}

int rbd_stat(rbd_image_t image, rbd_image_info_t *info, size_t infosize) {
  // info->order = BLOCK_SIZE_ORDER;
  // info->size = DISK_SIZE;
  // info->obj_size = 1 << BLOCK_SIZE_ORDER;

  struct bs3Stat_return ret = bs3Stat();
  info->size = ret.r0;
  info->obj_size = ret.r1;

  return 0;
}

int rbd_get_size(rbd_image_t image, uint64_t *size) {
  rbd_image_info_t info = {
      .size = DISK_SIZE}; // setting default size if call below fails
  rbd_stat(NULL, &info, sizeof(info));
  *size = info.size;
  return 0;
}

int rbd_resize(rbd_image_t image, uint64_t size) {
  return -1; // Not supported
}

/*
 * Read/Write functions
 */

// Copy data from buf to the scattered buffers in iov
static void copyToIov(void *buf, const struct iovec *iov, int iovcnt) {
  for (int i = 0; i < iovcnt; i++) {
    memcpy(iov[i].iov_base, buf, iov[i].iov_len);
    buf += iov[i].iov_len;
  }
}

// copy from the scattered buffers in iov into contiguous buffer buf
static void copyFromIov(void *buf, const struct iovec *iov, int iovcnt) {
  for (int i = 0; i < iovcnt; i++) {
    memcpy(buf, iov[i].iov_base, iov[i].iov_len);
    buf += iov[i].iov_len;
  }
}

void ignore_completion_callback(rbd_completion_t cb, void *arg) {}

// Called from Go code when it has completed an async read operation.
void go_aio_read_complete(AioCompletion *completion) {
  printf("go_aio_read_complete\n");
  if (completion->iovcnt > 0) {
    // For readv, copy from temp buf to user provided iov buffers
    copyToIov(completion->buf, completion->iov, completion->iovcnt);
    free(completion->buf);
  }
  // Call user callback
  completion->complete_cb(completion, completion->cb_arg);
  // completion gets freed after user callback
}

ssize_t rbd_read(rbd_image_t image, uint64_t ofs, size_t len, char *buf) {
  rbd_completion_t completion;
  rbd_aio_create_completion(NULL, ignore_completion_callback, &completion);

  rbd_aio_read(image, ofs, len, buf, completion);
  rbd_aio_wait_for_complete(completion);

  ssize_t ret = rbd_aio_get_return_value(completion);
  rbd_aio_release(completion);
  return ret;
}

int rbd_aio_read(rbd_image_t image, uint64_t off, size_t len, char *buf,
                 rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  GoSlice buffer = {.data = buf, .len = len, .cap = len};
  bs3Read(off, len, buffer, completion);
  return 0;
}

int rbd_aio_readv(rbd_image_t image, const struct iovec *iov, int iovcnt,
                  uint64_t off, rbd_completion_t c) {
  if (iovcnt == 1) {
    return rbd_aio_read(image, off, iov[0].iov_len, iov[0].iov_base, c);
  }
  // Go code only supports a single contiguous buffer but this function gives us
  // scattered buffers in iov. We therefore read into a contiguous buffer and
  // then copy into iov buffers in the callback.
  size_t len = 0;
  for (int i = 0; i < iovcnt; i++) {
    len += iov[i].iov_len;
  }
  void *buf = malloc(len);
  // indicate in the completion that buf needs to be copied into iov in the
  // callback
  AioCompletion *completion = (AioCompletion *)c;
  completion->iov = iov;
  completion->iovcnt = iovcnt;
  completion->buf = buf;

  return 0;
}

// Called from Go code when it has completed an async write operation.
void go_aio_write_complete(AioCompletion *completion) {
  printf("go_aio_write_complete\n");
  if (completion->iovcnt > 0) {
    // For writev, release the temp buffer
    free(completion->buf);
  }
  // Call user callback
  printf("Calling user callback\n");
  completion->complete_cb(completion, completion->cb_arg);
  printf("calling user callback finished\n");
  // completion gets freed after user callback
}

ssize_t rbd_write(rbd_image_t image, uint64_t ofs, size_t len,
                  const char *buf) {
  rbd_completion_t completion;
  rbd_aio_create_completion(NULL, ignore_completion_callback, &completion);

  rbd_aio_write(image, ofs, len, buf, completion);
  rbd_aio_wait_for_complete(completion);

  ssize_t ret = rbd_aio_get_return_value(completion);
  rbd_aio_release(completion);
  return ret;
}

int rbd_aio_write(rbd_image_t image, uint64_t off, size_t len, const char *buf,
                  rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  GoSlice buffer = {.data = buf, .len = len, .cap = len};
  bs3Write(off, len, buffer, completion);
  return 0;
}

int rbd_aio_writev(rbd_image_t image, const struct iovec *iov, int iovcnt,
                   uint64_t off, rbd_completion_t c) {
  if (iovcnt == 1) {
    return rbd_aio_write(image, off, iov[0].iov_len, iov[0].iov_base, c);
  }
  size_t len = 0;
  for (int i = 0; i < iovcnt; i++) {
    len += iov[i].iov_len;
  }
  void *buf = malloc(len);
  copyFromIov(buf, iov, iovcnt);
  AioCompletion *completion = (AioCompletion *)c;
  completion->iovcnt = iovcnt;
  completion->buf = buf;
  completion->iov = iov; // not used by write callback
  return 0;
}

int rbd_aio_discard(rbd_image_t image, uint64_t off, uint64_t len,
                    rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  completion->return_value = 1; // success
  completion->complete_cb(c, completion->cb_arg);
  return 0;
}
int rbd_aio_write_zeroes(rbd_image_t image, uint64_t off, size_t len,
                         rbd_completion_t c, int zero_flags, int op_flags) {
  AioCompletion *completion = (AioCompletion *)c;
  void *zeros = malloc(len);
  bzero(zeros, len);
  // Force callback to free our buffer
  completion->buf = zeros;
  completion->iovcnt = 1;

  return rbd_aio_write(image, off, len, zeros, c);
}

// Used by FIO in rbd.c function _fio_rbd_connect

int rbd_flush(rbd_image_t image) {

  return 0;
}


/*
 * Cache operations
 */

int rbd_aio_flush(rbd_image_t image, rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  completion->complete_cb(c, completion->cb_arg);
  return 0;
}
int rbd_invalidate_cache(rbd_image_t image) { return 0; }

/*
 * AIO completion functions
 */

int rbd_aio_create_completion(void *cb_arg, rbd_callback_t complete_cb,
                              rbd_completion_t *c) {

  AioCompletion *completion = malloc(sizeof(AioCompletion));
  completion->cb_arg = cb_arg;
  completion->complete_cb = complete_cb;
  completion->buf = NULL;
  completion->iov = NULL;
  completion->iovcnt = 0;
  *c = completion;
  return 0;
}

void rbd_aio_release(rbd_completion_t c) {
  // Generally ensure that c is not used anymore, esp in Go code
  free(c);
}

ssize_t rbd_aio_get_return_value(rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  return completion->return_value;
}

void *rbd_aio_get_arg(rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  return completion->cb_arg;
}

int rbd_aio_is_complete(rbd_completion_t c) {
  AioCompletion *completion = (AioCompletion *)c;
  return completion->return_value != 0;
}

int rbd_aio_wait_for_complete(rbd_completion_t c) {
  // perform spin loop on a return_value
  AioCompletion *completion = (AioCompletion *)c;
  while (completion->return_value == 0) {
    usleep(5);
  }
  return 0;
}

/*
 * Unsupported functions
 */

int rbd_snap_list(rbd_image_t image, rbd_snap_info_t *snaps, int *max_snaps) {
  return -1;
}
void rbd_snap_list_end(rbd_snap_info_t *snaps) {}
int rbd_snap_create(rbd_image_t image, const char *snapname) { return -1; }
int rbd_snap_remove(rbd_image_t image, const char *snapname) { return -1; }
int rbd_snap_rollback(rbd_image_t image, const char *snapname) { return -1; }

int rbd_encryption_format(rbd_image_t image, rbd_encryption_format_t format,
                          rbd_encryption_options_t opts, size_t opts_size) {
  return -ENOTSUP;
}
int rbd_encryption_load(rbd_image_t image, rbd_encryption_format_t format,
                        rbd_encryption_options_t opts, size_t opts_size) {
  return -ENOTSUP;
}

// For testing

void go_dummy_callback(AioCompletion *c) {
  puts("Go called the dummy callback.");
}

static void main() {

  printf("====Test: Pass args to Go, print them and modify a buffer\n");
  char buf[] = "This is a string";
  bs3ReadTest(45, sizeof buf, buf, 0);
  puts(buf);

  puts("====Test: Pass a GoSlice allocated in C directly to Go\n");
  char buf16[16] = {0};
  GoSlice slice = {.data = &buf16, .len = 2, .cap = 16};
  bs3WriteTest(0, 4096, slice);
  puts(buf16);
  printf("slice is now of length:%lld\n", slice.len);

  puts("====Test: Pass AioCompletion to Go. Modify it in Goroutine. Wait for "
       "completion\n");
  AioCompletion completion = {0};
  bs3Async(&completion);
  rbd_aio_wait_for_complete(&completion);
  printf("Go set AioCompletion filed from 0 to %ld\n", completion.return_value);

  puts("====Test: Call C callback function from Go\n");
  bs3CallbackTest(NULL);
  puts("bs3CallbackTest function returned. Wait for the callback and then "
       "press any key to exit\n");

  getchar();
}