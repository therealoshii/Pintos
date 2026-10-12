#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

typedef int fixed_t;
#define FP_SHIFT_AMOUNT 14

#define FP_CONST(A)      ((fixed_t)((A) << FP_SHIFT_AMOUNT))
#define FP_ADD(A,B)      ((A) + (B))
#define FP_ADD_MIX(A,B)  ((A) + ((B) << FP_SHIFT_AMOUNT))
#define FP_SUB(A,B)      ((A) - (B))
#define FP_SUB_MIX(A,B)  ((A) - ((B) << FP_SHIFT_AMOUNT))
#define FP_MULT_MIX(A,B) ((A) * (B))
#define FP_DIV_MIX(A,B)  ((A) / (B))
#define FP_MULT(A,B)     ((fixed_t)(((int64_t)(A)) * (B) >> FP_SHIFT_AMOUNT))
#define FP_DIV(A,B)      ((fixed_t)((((int64_t)(A)) << FP_SHIFT_AMOUNT) / (B)))
#define FP_INT_PART(A)   ((A) >> FP_SHIFT_AMOUNT)
#define FP_ROUND(A) \
  ((A) >= 0 ? (((A) + (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT) \
            : (((A) - (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT))

#endif /* threads/fixed-point.h */