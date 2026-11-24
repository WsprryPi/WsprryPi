// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include <stdint.h>
#include <stddef.h>
#ifdef WSPRRYPI_RECOVERY_NATIVE_BUILD
#define WRN_API __declspec(dllexport)
#else
#define WRN_API __declspec(dllimport)
#endif
#define WRN_CALL __stdcall
// Private C ABI, independent of control/storage schemas. Callers verify version
// and exact layout before accessing fields. No C++ object or allocator crosses.
struct WrnInfo {
    uint64_t volume;
    uint8_t file_id[16];
    uint64_t size;
    uint32_t links,attributes;
    int64_t modified_ticks,changed_ticks;
    uint32_t directory;
    char owner_sid[192];
};
#ifdef __cplusplus
extern "C" {
#endif
WRN_API uint32_t WRN_CALL wrn_version(void);
WRN_API uint32_t WRN_CALL wrn_info_size(void);
// Status: 0 success, 1 unsafe/unavailable, 2 absent, 3 invalid token/argument,
// 4 already exists, 5 owner busy, 6 cross-volume move. Tokens are opaque, process-local,
// and never serialized.
WRN_API uint32_t WRN_CALL wrn_check_parent(const char *,size_t);
WRN_API uint32_t WRN_CALL wrn_path_info(const char *,size_t,struct WrnInfo *,size_t);
// Open modes: 1 private read, 2 exclusive private create, 3 existing private RW.
WRN_API uint32_t WRN_CALL wrn_open(const char *,size_t,uint32_t,uint64_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_duplicate(uint64_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_file_info(uint64_t,struct WrnInfo *,size_t);
WRN_API uint32_t WRN_CALL wrn_handle_info(uint64_t,struct WrnInfo *,size_t);
WRN_API uint32_t WRN_CALL wrn_flush(uint64_t);
WRN_API uint32_t WRN_CALL wrn_close(uint64_t);
// Directory modes: 0 inspect, 1 exclusive create, 2 create or validate existing.
WRN_API uint32_t WRN_CALL wrn_directory(const char *,size_t,uint32_t);
WRN_API uint32_t WRN_CALL wrn_directory_flush(const char *,size_t);
WRN_API uint32_t WRN_CALL wrn_record_read(const char *,size_t,void *,size_t,uint64_t *,char *,size_t,uint32_t *);
WRN_API uint32_t WRN_CALL wrn_record_publish(const char *,size_t,uint32_t,const void *,size_t,const char *,size_t,const void *,size_t);
// Returns owned noninheritable binary pipe handles with an explicit private ACL.
WRN_API uint32_t WRN_CALL wrn_pipe(uint64_t *,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_generation_acquire(const char *,size_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_generation_close(uint64_t);
// Existing stopped SQL ownership requires a live generation-admission token.
// The database lease retains that admission until it is explicitly closed.
WRN_API uint32_t WRN_CALL wrn_database_acquire(uint64_t,const char *,size_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_database_verify(uint64_t,const char *,size_t);
WRN_API uint32_t WRN_CALL wrn_database_move(uint64_t,const char *,size_t,const char *,size_t);
WRN_API uint32_t WRN_CALL wrn_database_close(uint64_t);
// Only a newly created, empty inactive snapshot may provision its marker.
WRN_API uint32_t WRN_CALL wrn_database_initialize(const char *,size_t);
// Removable private directory admission; close on the acquiring native thread.
WRN_API uint32_t WRN_CALL wrn_directory_acquire(const char *,size_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_directory_close(uint64_t);
// Join an established installation witness without creating or repairing it.
WRN_API uint32_t WRN_CALL wrn_installation_acquire(const char *,size_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_installation_close(uint64_t);
// Private backend-owner stream. Attach only an inherited service listener;
// connect only to the current fixed service PID. Byte counts remain bounded.
WRN_API uint32_t WRN_CALL wrn_owner_attach(uint64_t,uint32_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_owner_connect(uint32_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_owner_accept(uint64_t);
WRN_API uint32_t WRN_CALL wrn_owner_disconnect(uint64_t);
WRN_API uint32_t WRN_CALL wrn_owner_transfer(uint64_t,void *,size_t,uint32_t,uint32_t);
WRN_API uint32_t WRN_CALL wrn_owner_lost(uint64_t,uint32_t *);
WRN_API uint32_t WRN_CALL wrn_owner_close(uint64_t);
// Fixed installed controller service only; admission requires maintenance SID.
// Start modes: 1 normal controller, 2 offline owner helper with a 32-hex nonce.
WRN_API uint32_t WRN_CALL wrn_service_open(uint64_t *);
WRN_API uint32_t WRN_CALL wrn_service_query(uint64_t,uint32_t *,uint32_t *);
WRN_API uint32_t WRN_CALL wrn_service_start(uint64_t,uint32_t,const char *,size_t,uint32_t *);
WRN_API uint32_t WRN_CALL wrn_service_stop(uint64_t,uint32_t);
WRN_API uint32_t WRN_CALL wrn_service_close(uint64_t);
// Fixed installed backend commands; poll returns at most 8192 bytes per call.
WRN_API uint32_t WRN_CALL wrn_backend_start(const char *,size_t,const char *,size_t,const char *,size_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_backend_poll(uint64_t,void *,size_t,uint32_t *,uint32_t *,uint32_t *);
WRN_API uint32_t WRN_CALL wrn_backend_close(uint64_t);
// Fixed encryption helper only; secret bytes remain in the inherited pipe.
WRN_API uint32_t WRN_CALL wrn_crypto_start(const char *,size_t,const char *,size_t,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_crypto_wait(uint64_t,uint32_t,uint32_t *);
WRN_API uint32_t WRN_CALL wrn_crypto_close(uint64_t);
// Private completed-artifact retirement. DELETE ownership excludes existing
// downloads and prevents new readers, replacement, and rename until retirement.
WRN_API uint32_t WRN_CALL wrn_prune_open(const char *,size_t,uint64_t *);
WRN_API uint32_t WRN_CALL wrn_prune_check(uint64_t,const char *,size_t);
WRN_API uint32_t WRN_CALL wrn_prune_remove(uint64_t);
WRN_API uint32_t WRN_CALL wrn_prune_close(uint64_t);
#ifdef __cplusplus
}
#endif
#endif
