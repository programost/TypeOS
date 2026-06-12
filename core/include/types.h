#ifndef TYPES_H
#define TYPES_H

// Беззнаковые целые типы (Unsigned)
typedef unsigned char          uint8_t;
typedef unsigned short         uint16_t;
typedef unsigned int           uint32_t;
typedef unsigned long long     uint64_t;

// Знаковые целые типы (Signed)
typedef signed char            int8_t;
typedef signed short           int16_t;
typedef signed int             int32_t;
typedef signed long long       int64_t;

// Типы для размеров памяти и указателей
typedef unsigned long long     size_t;
typedef signed long long       ssize_t;
typedef unsigned long long     uintptr_t;
typedef signed long long       intptr_t;

// Булевый тип
typedef unsigned char          bool;
#define true                   (1)
#define false                  (0)

// Значение для пустых указателей
#define NULL                   ((void*)0)

#endif // TYPES_H
