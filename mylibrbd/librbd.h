// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.	See file COPYING.
 *
 */

#ifndef CEPH_LIBRBD_H
#define CEPH_LIBRBD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long size_t;
typedef unsigned long uint64_t;
typedef long ssize_t; 
typedef long int64_t;

/* Structure for scatter/gather I/O.  From sys/uio.h */
struct iovec
  {
    void *iov_base;	/* Pointer to data.  */
    size_t iov_len;	/* Length of data.  */
  };

#define LIBRBD_VER_MAJOR 1
#define LIBRBD_VER_MINOR 17
#define LIBRBD_VER_EXTRA 0

#define LIBRBD_VERSION(maj, min, extra) ((maj << 16) + (min << 8) + extra)

#define LIBRBD_VERSION_CODE LIBRBD_VERSION(LIBRBD_VER_MAJOR, LIBRBD_VER_MINOR, LIBRBD_VER_EXTRA)


#define CEPH_RBD_API
#define CEPH_RBD_DEPRECATED


/**
 * COPIED FROM librados.h
 * @typedef rados_ioctx_t
 *
 * An io context encapsulates a few settings for all I/O operations
 * done on it:
 * - pool - set when the io context is created (see rados_ioctx_create())
 * - snapshot context for writes (see
 *   rados_ioctx_selfmanaged_snap_set_write_ctx())
 * - snapshot id to read from (see rados_ioctx_snap_set_read())
 * - object locator for all single-object operations (see
 *   rados_ioctx_locator_set_key())
 * - namespace for all single-object operations (see
 *   rados_ioctx_set_namespace()).  Set to LIBRADOS_ALL_NSPACES
 *   before rados_nobjects_list_open() will list all objects in all
 *   namespaces.
 *
 * @warning Changing any of these settings is not thread-safe -
 * librados users must synchronize any of these changes on their own,
 * or use separate io contexts for each thread
 */
typedef void *rados_ioctx_t;

typedef void *rbd_image_t;

typedef void *rbd_completion_t;
typedef void (*rbd_callback_t)(rbd_completion_t cb, void *arg);

typedef struct {
    //User supplied custom argument for the callback
    void *cb_arg;
    
    //User supplied callback
    rbd_callback_t complete_cb;
    
    //0 value indicates queued/processing stage
    //-ve values indicate failed
    //+ve values indicate successful completion
    ssize_t return_value;

    //Following fields are only needed when we have to copy from buf to iov for readv function. This operation is only needed if iovcnt > 0
    void* buf;
    const struct iovec* iov;
    int iovcnt;
  } AioCompletion;

typedef struct {
  uint64_t id;
  uint64_t size;
  const char *name;
} rbd_snap_info_t;


#define RBD_MAX_IMAGE_NAME_SIZE 96
#define RBD_MAX_BLOCK_NAME_SIZE 24


typedef struct {
  uint64_t size;
  uint64_t obj_size;
  uint64_t num_objs;
  int order;
  char block_name_prefix[RBD_MAX_BLOCK_NAME_SIZE]; /* deprecated */
  int64_t parent_pool;                             /* deprecated */
  char parent_name[RBD_MAX_IMAGE_NAME_SIZE];       /* deprecated */
} rbd_image_info_t;




CEPH_RBD_API void rbd_version(int *major, int *minor, int *extra);

/* image options */
enum {
  RBD_IMAGE_OPTION_FORMAT = 0,
  RBD_IMAGE_OPTION_FEATURES = 1,
  RBD_IMAGE_OPTION_ORDER = 2,
  RBD_IMAGE_OPTION_STRIPE_UNIT = 3,
  RBD_IMAGE_OPTION_STRIPE_COUNT = 4,
  RBD_IMAGE_OPTION_JOURNAL_ORDER = 5,
  RBD_IMAGE_OPTION_JOURNAL_SPLAY_WIDTH = 6,
  RBD_IMAGE_OPTION_JOURNAL_POOL = 7,
  RBD_IMAGE_OPTION_FEATURES_SET = 8,
  RBD_IMAGE_OPTION_FEATURES_CLEAR = 9,
  RBD_IMAGE_OPTION_DATA_POOL = 10,
  RBD_IMAGE_OPTION_FLATTEN = 11,
  RBD_IMAGE_OPTION_CLONE_FORMAT = 12,
  RBD_IMAGE_OPTION_MIRROR_IMAGE_MODE = 13,
};

/* rbd_write_zeroes / rbd_aio_write_zeroes flags */
enum {
  RBD_WRITE_ZEROES_FLAG_THICK_PROVISION = (1U<<0), /* fully allocated zeroed extent */
};

typedef enum {
    RBD_ENCRYPTION_FORMAT_LUKS1 = 0,
    RBD_ENCRYPTION_FORMAT_LUKS2 = 1
} rbd_encryption_format_t;

typedef enum {
    RBD_ENCRYPTION_ALGORITHM_AES128 = 0,
    RBD_ENCRYPTION_ALGORITHM_AES256 = 1
} rbd_encryption_algorithm_t;

typedef void *rbd_encryption_options_t;

typedef struct {
    rbd_encryption_algorithm_t alg;
    const char* passphrase;
    size_t passphrase_size;
} rbd_encryption_luks1_format_options_t;

typedef struct {
    rbd_encryption_algorithm_t alg;
    const char* passphrase;
    size_t passphrase_size;
} rbd_encryption_luks2_format_options_t;



/**
 * create new rbd image
 *
 * The stripe_unit must be a factor of the object size (1 << order).
 * The stripe_count can be one (no intra-object striping) or greater
 * than one.  The RBD_FEATURE_STRIPINGV2 must be specified if the
 * stripe_unit != the object size and the stripe_count is != 1.
 *
 * @param io ioctx
 * @param name image name
 * @param size image size in bytes
 * @param features initial feature bits
 * @param order object/block size, as a power of two (object size == 1 << order)
 * @param stripe_unit stripe unit size, in bytes.
 * @param stripe_count number of objects to stripe over before looping
 * @return 0 on success, or negative error code
 */
CEPH_RBD_API int rbd_create(rados_ioctx_t io, const char *name, uint64_t size,int *order);



CEPH_RBD_API int rbd_remove(rados_ioctx_t io, const char *name);



/**
 * Open an image in read-only mode.
 *
 * This is intended for use by clients that cannot write to a block
 * device due to cephx restrictions. There will be no watch
 * established on the header object, since a watch is a write. This
 * means the metadata reported about this image (parents, snapshots,
 * size, etc.) may become stale. This should not be used for
 * long-running operations, unless you can be sure that one of these
 * properties changing is safe.
 *
 * Attempting to write to a read-only image will return -EROFS.
 *
 * @param io ioctx to determine the pool the image is in
 * @param name image name
 * @param image where to store newly opened image handle
 * @param snap_name name of snapshot to open at, or NULL for no snapshot
 * @returns 0 on success, negative error code on failure
 */

CEPH_RBD_API int rbd_open(rados_ioctx_t io, const char *name,
                          rbd_image_t *image, const char *snap_name);
CEPH_RBD_API int rbd_close(rbd_image_t image);
CEPH_RBD_API int rbd_resize(rbd_image_t image, uint64_t size);
CEPH_RBD_API int rbd_stat(rbd_image_t image, rbd_image_info_t *info,
                          size_t infosize);
CEPH_RBD_API int rbd_get_size(rbd_image_t image, uint64_t *size);


/* encryption */
CEPH_RBD_API int rbd_encryption_format(rbd_image_t image,
                                       rbd_encryption_format_t format,
                                       rbd_encryption_options_t opts,
                                       size_t opts_size);
CEPH_RBD_API int rbd_encryption_load(rbd_image_t image,
                                     rbd_encryption_format_t format,
                                     rbd_encryption_options_t opts,
                                     size_t opts_size);

/* snapshots */
CEPH_RBD_API int rbd_snap_list(rbd_image_t image, rbd_snap_info_t *snaps,
                               int *max_snaps);
CEPH_RBD_API void rbd_snap_list_end(rbd_snap_info_t *snaps);
CEPH_RBD_API int rbd_snap_create(rbd_image_t image, const char *snapname);
CEPH_RBD_API int rbd_snap_remove(rbd_image_t image, const char *snapname);
CEPH_RBD_API int rbd_snap_rollback(rbd_image_t image, const char *snapname);


/* I/O */
CEPH_RBD_API ssize_t rbd_read(rbd_image_t image, uint64_t ofs, size_t len,
                              char *buf);

CEPH_RBD_API ssize_t rbd_write(rbd_image_t image, uint64_t ofs, size_t len,
                               const char *buf);

CEPH_RBD_API int rbd_aio_write(rbd_image_t image, uint64_t off, size_t len,
                               const char *buf, rbd_completion_t c);

CEPH_RBD_API int rbd_aio_writev(rbd_image_t image, const struct iovec *iov,
                                int iovcnt, uint64_t off, rbd_completion_t c);
CEPH_RBD_API int rbd_aio_read(rbd_image_t image, uint64_t off, size_t len,
                              char *buf, rbd_completion_t c);
CEPH_RBD_API int rbd_aio_readv(rbd_image_t image, const struct iovec *iov,
                               int iovcnt, uint64_t off, rbd_completion_t c);
CEPH_RBD_API int rbd_aio_discard(rbd_image_t image, uint64_t off, uint64_t len,
                                 rbd_completion_t c);
CEPH_RBD_API int rbd_aio_write_zeroes(rbd_image_t image, uint64_t off,
                                      size_t len, rbd_completion_t c,
                                      int zero_flags, int op_flags);


CEPH_RBD_API int rbd_aio_create_completion(void *cb_arg,
                                           rbd_callback_t complete_cb,
                                           rbd_completion_t *c);
CEPH_RBD_API int rbd_aio_is_complete(rbd_completion_t c);
CEPH_RBD_API int rbd_aio_wait_for_complete(rbd_completion_t c);
CEPH_RBD_API ssize_t rbd_aio_get_return_value(rbd_completion_t c);
CEPH_RBD_API void *rbd_aio_get_arg(rbd_completion_t c);
CEPH_RBD_API void rbd_aio_release(rbd_completion_t c);


#ifdef __cplusplus
}
#endif

#endif /* CEPH_LIBRBD_H */
