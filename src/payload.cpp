// ---------------------------------------------------------------------------
//  payload.cpp - reader for the self extracting portable package.
//
//  Layout of the package:
//
//    [ PE image of ocshell.exe ]
//    [ compressed blocks, one per file ]
//    [ manifest ]
//    [ trailer: "OCSHPAY2" | uint64 manifest offset | uint32 manifest length |
//              uint32 crc32 of the manifest ]
//
//  Manifest:
//    uint32 file count
//    per file: uint16 name length, name (utf-8), uint64 block offset,
//              uint64 size, uint64 stored size, uint32 crc32, uint8 method
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

#include "payload.h"
#include "zlib.h"

#define TRAILER_MAGIC "OCSHPAY2"
#define TRAILER_SIZE 24

struct PayloadFile {
  char name[512];
  unsigned long long offset;
  unsigned long long size;
  unsigned long long stored;
  unsigned int crc;
  unsigned char method;
};

// Diagnostic log (helps when the launcher misbehaves on a fresh Windows 7).
static void plog(const wchar_t* fmt, ...) {
  wchar_t path[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, path);
  if (!n || n + 32 >= MAX_PATH) return;
  wcscat(path, L"ocshell-extract.log");
  FILE* f = _wfopen(path, L"a, ccs=UTF-8");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfwprintf(f, fmt, ap);
  va_end(ap);
  fwprintf(f, L"\n");
  fclose(f);
}

static unsigned int read_u32(const unsigned char* p) {
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned long long read_u64(const unsigned char* p) {
  return (unsigned long long)read_u32(p) | ((unsigned long long)read_u32(p + 4) << 32);
}

static void make_dirs(const wchar_t* path) {
  wchar_t* copy = (wchar_t*)malloc((wcslen(path) + 2) * sizeof(wchar_t));
  if (!copy) return;
  wcscpy(copy, path);
  for (wchar_t* p = copy + 3; *p; p++) {
    if (*p == L'\\') {
      *p = 0;
      CreateDirectoryW(copy, NULL);
      *p = L'\\';
    }
  }
  CreateDirectoryW(copy, NULL);
  free(copy);
}

static int write_file(const wchar_t* dir, const PayloadFile* file, const unsigned char* data) {
  wchar_t wname[512];
  size_t n = strlen(file->name);
  if (n >= sizeof(wname) / sizeof(wname[0])) return -1;
  for (size_t i = 0; i < n; i++) wname[i] = (wchar_t)(unsigned char)file->name[i];
  wname[n] = 0;
  size_t need = (wcslen(dir) + wcslen(wname) + 2) * sizeof(wchar_t);
  wchar_t* full = (wchar_t*)malloc(need);
  if (!full) return -1;
  wcscpy(full, dir);
  wcscat(full, L"\\");
  wcscat(full, wname);
  // create the directory part only
  wchar_t* slash = wcsrchr(full, L'\\');
  if (slash) {
    *slash = 0;
    make_dirs(full);
    *slash = L'\\';
  }

  HANDLE h = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    fwprintf(stderr, L"[payload] cannot create %ls (error %lu)\n", full, (unsigned long)GetLastError());
    free(full);
    return -1;
  }
  free(full);
  DWORD written = 0;
  BOOL ok = WriteFile(h, data, (DWORD)file->size, &written, NULL);
  CloseHandle(h);
  return ok ? 0 : -1;
}

int oc_payload_extract(const wchar_t* target_dir) {
  wchar_t path[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
  if (!n) return -1;
  HANDLE self = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (self == INVALID_HANDLE_VALUE) return -1;

  LARGE_INTEGER size;
  GetFileSizeEx(self, &size);
  if ((unsigned long long)size.QuadPart < TRAILER_SIZE) {
    CloseHandle(self);
    return -1;
  }

  unsigned char trailer[TRAILER_SIZE];
  LARGE_INTEGER off;
  DWORD got = 0;
  off.QuadPart = size.QuadPart - TRAILER_SIZE;
  SetFilePointerEx(self, off, NULL, FILE_BEGIN);
  if (!ReadFile(self, trailer, TRAILER_SIZE, &got, NULL) || got != TRAILER_SIZE) {
    CloseHandle(self);
    return -1;
  }
  if (memcmp(trailer, TRAILER_MAGIC, 8) != 0) {
    CloseHandle(self);
    return -1;  // not a self extracting package
  }
  plog(L"[payload] package ok, size=%llu", (unsigned long long)size.QuadPart);
  unsigned long long manifest_offset = read_u64(trailer + 8);
  unsigned int manifest_length = read_u32(trailer + 16);
  unsigned int manifest_crc = read_u32(trailer + 20);
  if (manifest_offset + manifest_length > (unsigned long long)size.QuadPart) {
    CloseHandle(self);
    return -1;
  }

  unsigned char* manifest = (unsigned char*)malloc(manifest_length);
  if (!manifest) {
    CloseHandle(self);
    return -1;
  }
  off.QuadPart = (LONGLONG)manifest_offset;
  SetFilePointerEx(self, off, NULL, FILE_BEGIN);
  if (!ReadFile(self, manifest, manifest_length, &got, NULL) || got != manifest_length) {
    free(manifest);
    CloseHandle(self);
    return -1;
  }
  if (crc32(0, manifest, manifest_length) != manifest_crc) {
    free(manifest);
    CloseHandle(self);
    return -1;
  }

  unsigned int pos = 4;
  unsigned int count = read_u32(manifest);
  plog(L"[payload] manifest: %u file(s)", count);
  for (unsigned int i = 0; i < count; i++) {
    if (pos + 2 > manifest_length) break;
    unsigned int name_len = (unsigned char)manifest[pos] | ((unsigned char)manifest[pos + 1] << 8);
    pos += 2;
    if (pos + name_len > manifest_length) break;
    PayloadFile file;
    memset(&file, 0, sizeof(file));
    if (name_len >= sizeof(file.name)) name_len = sizeof(file.name) - 1;
    memcpy(file.name, manifest + pos, name_len);
    pos += name_len;
    if (pos + 29 > manifest_length) break;
    file.offset = read_u64(manifest + pos);
    file.size = read_u64(manifest + pos + 8);
    file.stored = read_u64(manifest + pos + 16);
    file.crc = read_u32(manifest + pos + 24);
    file.method = manifest[pos + 28];
    pos += 29;

    unsigned char* stored = (unsigned char*)malloc(file.stored ? (size_t)file.stored : 1);
    if (!stored) {
      fwprintf(stderr, L"[payload] out of memory\n");
      break;
    }
    off.QuadPart = (LONGLONG)file.offset;
    SetFilePointerEx(self, off, NULL, FILE_BEGIN);
    if (!ReadFile(self, stored, (DWORD)file.stored, &got, NULL) || got != (DWORD)file.stored) {
      fwprintf(stderr, L"[payload] cannot read block for %s\n", file.name);
      free(stored);
      break;
    }

    unsigned char* plain = (unsigned char*)malloc(file.size ? (size_t)file.size : 1);
    if (!plain) {
      free(stored);
      break;
    }
    if (file.method == 0) {
      memcpy(plain, stored, (size_t)file.size);
    } else {
      uLongf out_len = (uLongf)file.size;
      if (uncompress(plain, &out_len, stored, (uLong)file.stored) != Z_OK) {
        fwprintf(stderr, L"[payload] cannot inflate %s\n", file.name);
        free(stored);
        free(plain);
        break;
      }
    }
    free(stored);
    if (crc32(0, plain, (uInt)file.size) != file.crc) {
      fwprintf(stderr, L"[payload] checksum mismatch for %s\n", file.name);
      free(plain);
      break;
    }
    if (write_file(target_dir, &file, plain) != 0) {
      fwprintf(stderr, L"[payload] cannot write %s\n", file.name);
      free(plain);
      break;
    }
    plog(L"[payload] wrote %S (%llu bytes)", file.name, file.size);
    free(plain);
  }

  free(manifest);
  CloseHandle(self);
  return 0;
}
