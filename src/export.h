// TODO 这个文件中应该作为cmake自动生成的
#ifndef MEHA_EXPORT_H
#define MEHA_EXPORT_H

#ifdef MEHA_STATIC_DEFINE
#define MEHA_EXPORT
#define MEHA_NO_EXPORT
#else
#ifndef MEHA_EXPORT
#ifdef MEHA_EXPORTS
/* We are building this library */
#define MEHA_EXPORT __attribute__((visibility("default")))
#else
/* We are using this library */
#define MEHA_EXPORT __attribute__((visibility("default")))
#endif
#endif

#ifndef MEHA_NO_EXPORT
#define MEHA_NO_EXPORT __attribute__((visibility("hidden")))
#endif

#ifndef MEHA_CTOR
#define MEHA_CTOR __attribute__((constructor))
#endif
#endif

#ifndef MEHA_DEPRECATED
#define MEHA_DEPRECATED __attribute__((__deprecated__))
#endif

#ifndef MEHA_DEPRECATED_EXPORT
#define MEHA_DEPRECATED_EXPORT MEHA_EXPORT MEHA_DEPRECATED
#endif

#ifndef MEHA_DEPRECATED_NO_EXPORT
#define MEHA_DEPRECATED_NO_EXPORT MEHA_NO_EXPORT MEHA_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#ifndef MEHA_NO_DEPRECATED
#define MEHA_NO_DEPRECATED
#endif
#endif

#endif /* MEHA_EXPORT_H */
