/* app/mpx/mpx_compat.h - 杰理工具链下的 ABI 头包含层
 *
 * 必须先含 os_api.h(杰理 bool/uint8_t typedef),再把 offsetof 恢复为
 * builtin(杰理 typedef.h 的取址版非常量表达式,过不了 static_assert),
 * 最后才展开 micropixel_abi.h。所有 mpx 文件统一经此头引用 ABI。
 */
#ifndef _MPX_COMPAT_H_
#define _MPX_COMPAT_H_

#include "os/os_api.h"

#ifdef offsetof
#undef offsetof
#endif
#define offsetof(type, memb) __builtin_offsetof(type, memb)

#include "micropixel_abi.h"

#endif /* _MPX_COMPAT_H_ */
