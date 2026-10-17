/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include "resource_unpack.h"

#include "resource.h"
#include "resource_storage_impl.h"

#include <stdint.h>
#include <string.h>

#include <pbl/logging/logging.h>
#include <pbl/services/filesystem/pfs.h>

#include <kernel/pbl_malloc.h>
#include <uzlib.h>

#define UNPACK_MAGIC       "PBLZ"
#define UNPACK_WINDOW_SIZE 4096
#define UNPACK_CHUNK_SIZE  256

typedef struct PBL_PACKED {
  char magic[4];
  uint32_t pack_length;
  uint32_t pack_crc;
} UnpackHeader;

typedef struct {
  struct uzlib_uncomp d; // first, so the read callback can find the rest
  uint32_t resource_id;
  uint32_t offset;
  uint32_t end;
  uint8_t in[UNPACK_CHUNK_SIZE];
  uint8_t out[UNPACK_CHUNK_SIZE];
  uint8_t window[UNPACK_WINDOW_SIZE];
} Inflater;

static int prv_read_cb(struct uzlib_uncomp *d) {
  Inflater *inf = (Inflater *)d;
  uint32_t len = inf->end - inf->offset;
  if (len == 0) {
    return -1;
  }
  if (len > sizeof(inf->in)) {
    len = sizeof(inf->in);
  }
  if (resource_load_byte_range_system(SYSTEM_APP, inf->resource_id, inf->offset, inf->in, len) !=
      len) {
    return -1;
  }
  inf->offset += len;
  d->source = &inf->in[1];
  d->source_limit = &inf->in[len];
  return inf->in[0];
}

//! Whether the file already holds the pack with this content CRC.
static bool prv_file_is_current(const char *name, const UnpackHeader *hdr) {
  const int fd = pfs_open(name, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd < 0) {
    return false;
  }
  ResourceManifest manifest = {};
  const bool current = (pfs_get_file_size(fd) == hdr->pack_length) &&
                       (pfs_read(fd, &manifest, sizeof(manifest)) == sizeof(manifest)) &&
                       (manifest.version.crc == hdr->pack_crc);
  pfs_close(fd);
  return current;
}

static bool prv_inflate_into(int fd, uint32_t resource_id, uint32_t length, uint32_t pack_length) {
  Inflater *inf = kernel_malloc(sizeof(Inflater));
  if (!inf) {
    return false;
  }
  *inf = (Inflater){
    .resource_id = resource_id,
    .offset = sizeof(UnpackHeader),
    .end = length,
  };
  uzlib_init();
  uzlib_uncompress_init(&inf->d, inf->window, sizeof(inf->window));
  inf->d.source_read_cb = prv_read_cb;

  uint32_t written = 0;
  int res;
  do {
    inf->d.dest_start = inf->d.dest = inf->out;
    inf->d.dest_limit = &inf->out[sizeof(inf->out)];
    res = uzlib_uncompress(&inf->d);
    const int n = inf->d.dest - inf->out;
    if ((res < 0) || (written + n > pack_length) || (pfs_write(fd, inf->out, n) != n)) {
      res = -1;
      break;
    }
    written += n;
  } while (res != TINF_DONE);

  kernel_free(inf);
  return (res == TINF_DONE) && (written == pack_length);
}

static void prv_unpack(const UnpackedResourceFile *file) {
  const char *name = file->file->name;
  const uint32_t id = file->packed_resource_id;
  const size_t length = resource_size(SYSTEM_APP, id);

  UnpackHeader hdr;
  if ((length <= sizeof(hdr)) ||
      (resource_load_byte_range_system(SYSTEM_APP, id, 0, (uint8_t *)&hdr, sizeof(hdr)) !=
       sizeof(hdr)) ||
      (memcmp(hdr.magic, UNPACK_MAGIC, sizeof(hdr.magic)) != 0)) {
    PBL_LOG_ERR("Packed resource file %s is invalid", name);
    return;
  }
  if (prv_file_is_current(name, &hdr)) {
    return;
  }

  PBL_LOG_INFO("Unpacking resource file %s", name);
  // Overwrite keeps the old version readable until the new one is complete.
  int fd = pfs_open(name, OP_FLAG_WRITE | OP_FLAG_OVERWRITE, FILE_TYPE_STATIC, hdr.pack_length);
  if (fd == E_DOES_NOT_EXIST) {
    fd = pfs_open(name, OP_FLAG_WRITE, FILE_TYPE_STATIC, hdr.pack_length);
  }
  if (fd < 0) {
    PBL_LOG_ERR("Could not create resource file %s: %d", name, fd);
    return;
  }
  if (prv_inflate_into(fd, id, length, hdr.pack_length)) {
    pfs_close(fd);
  } else {
    PBL_LOG_ERR("Unpacking resource file %s failed", name);
    pfs_close_and_remove(fd);
  }
}

void resource_unpack_init(void) {
  for (uint32_t i = 0; i < g_num_unpacked_resource_files; i++) {
    prv_unpack(&g_unpacked_resource_files[i]);
  }
}
