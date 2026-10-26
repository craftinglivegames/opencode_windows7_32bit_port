// ---------------------------------------------------------------------------
//  payload.h - reader for the self extracting portable package.
// ---------------------------------------------------------------------------
#ifndef OCSHELL_PAYLOAD_H
#define OCSHELL_PAYLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

// Unpacks the runtime that is appended to this executable into `target_dir`.
// Returns 0 on success.
int oc_payload_extract(const wchar_t* target_dir);

#ifdef __cplusplus
}
#endif

#endif
