#ifndef DLLEXPORT_H
#define DLLEXPORT_H
#ifdef emacs
#define DLLEXPORT _declspec(dllexport)
#else
#define DLLEXPORT _declspec(dllimport)
#endif

#endif
