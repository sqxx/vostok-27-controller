/* MathHelpers.h
   Макросы для упрощения математических проверок и вычислений

   Исходный код станции Восток-27
*/

#ifndef MATH_HELPERS_H
#define MATH_HELPERS_H

/* Округляет float до int

   Arduino не умеет округлять float до int, учитывая значение дробной части.
   По правилам, если дробная часть больше или равна 0.5, то округляется вверх.
*/
#define FLOAT_TO_INT(x) (((((uint32_t)(x * 100)) % 100) >= 50 ? 1 : 0) + ((uint32_t) x))

// Проверяет, входит ли число в диапазон с включением границ
#define IS_IN_RANGE_IN(n, left, right) (n >= left && n <= right)

// Проверяет, входит ли число в диапазон без включения границ
#define IS_IN_RANGE_EX(n, left, right) (n > left && x < right)

#endif
