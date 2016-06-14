/*
 * Copyright (c) 2016, Circonus, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_metric_rollup.h"
#include <assert.h>
#define private_nan 0


static double
metric_value_double(noit_metric_value_t *v) {
  switch(v->type) {
    case METRIC_INT32: return (double)v->value.v_int32;
    case METRIC_UINT32: return (double)v->value.v_uint32;
    case METRIC_INT64: return (double)v->value.v_int64;
    case METRIC_UINT64: return (double)v->value.v_uint64;
    case METRIC_DOUBLE: return v->value.v_double;
    default: ;

  }
  return private_nan;
}
static int64_t
metric_value_int64(noit_metric_value_t *v) {
  switch(v->type) {
    case METRIC_INT32: return (int64_t)v->value.v_int32;
    case METRIC_UINT32: return (int64_t)v->value.v_uint32;
    case METRIC_INT64: return v->value.v_int64;
    default: ;
  }
  abort();
}
static uint64_t
metric_value_uint64(noit_metric_value_t *v) {
  switch(v->type) {
    case METRIC_INT32:
      assert(v->value.v_int32 >= 0);
      return (uint64_t)v->value.v_int32;
    case METRIC_UINT32: return (uint64_t)v->value.v_uint32;
    case METRIC_INT64:
      assert(v->value.v_int64 >= 0);
      return (uint64_t)v->value.v_int64;
    case METRIC_UINT64: return v->value.v_uint64;
    default: ;
  }
  abort();
}
static int
metric_value_is_negative(noit_metric_value_t *v) {
  switch(v->type) {
    case METRIC_UINT32:
    case METRIC_UINT64: return 0;
    case METRIC_INT32:
      return (v->value.v_int32 < 0);
    case METRIC_INT64:
      return (v->value.v_int64 < 0);
    case METRIC_DOUBLE:
      return (v->value.v_double < 0);
    default: ;
  }
  abort();
}

static void
calculate_change(noit_metric_value_t *v1, noit_metric_value_t *v2,
                 double *dy, int *dt) {
  *dt = v2->whence_ms - v1->whence_ms;
  if(v1->type == METRIC_ABSENT || v1->type == METRIC_NULL ||
     v1->type == METRIC_STRING || v2->type == METRIC_ABSENT ||
     v2->type == METRIC_NULL || v2->type == METRIC_STRING) {
    *dy = private_nan;
    return;
  }

  /* if either is a double, just do double math */
  if(v1->type == METRIC_DOUBLE || v2->type == METRIC_DOUBLE)
    goto double_math;

  /* if neither are uint64, then they'll fit in a signed 64bit int */
  if(v1->type != METRIC_UINT64 && v2->type != METRIC_UINT64) {
    int64_t diff, v1i, v2i;
    v1i = metric_value_int64(v1);
    v2i = metric_value_int64(v2);
    diff = v2i - v1i;
    /* overflows */
    if(v1i > v2i && diff > 0) goto double_math;
    if(v2i > v1i && diff < 0) goto double_math;
    *dy = (double)diff;
    return;
  }

  /* At this point, we know at least one is a unit64 */

  /* First handle the "both are positive" case */
  if((v2->type == METRIC_UINT64 && v1->type == METRIC_UINT64) ||
     (!metric_value_is_negative(v2) && !metric_value_is_negative(v1))) {
    /* They can both go into a uint64 */
    uint64_t v1u, v2u, diffu;
    v1u = metric_value_uint64(v1);
    v2u = metric_value_uint64(v2);
    if(v2u >= v1u) {
      diffu = v2u - v1u;
      *dy = diffu;
      return;
    }
    else {
      int64_t diffi;
      diffu = v1u - v2u;
      diffi = -diffu;
      if(diffi > 0) goto double_math; /* rolled */
      *dy = diffi;
      return;
    }
  }

  /* here, one is a uint64 and the other negative. We didn't want to use
   * double math as the different between two close values far form zero
   * is in accurate.  Here, two close values are definitively near zero.
   * do we use double math.
   */

 double_math:
  *dy = metric_value_double(v2) - metric_value_double(v1);
  return;
}

static int nnt_multitype_accum_counts(nnt_multitype *a, int a_count,
                               int a_drun, int a_crun,
                               const nnt_multitype *v, int v_count,
                               int v_drun, int v_crun) {
  int count;
  double avg, avg_l, avg_r;
  double stddev;

  /* sum of counts */
  count = a_count + v_count;

  /* extract double form of accumulator and new value */
  switch(a->type) {
    case METRIC_INT32: avg_l = a->value.v_int32; break;
    case METRIC_UINT32: avg_l = a->value.v_uint32; break;
    case METRIC_INT64: avg_l = a->value.v_int64; break;
    case METRIC_UINT64: avg_l = a->value.v_uint64; break;
    case METRIC_DOUBLE: avg_l = a->value.v_double; break;
    default: avg_l = 0.0;
  }
  switch(v->type) {
    case METRIC_INT32: avg_r = v->value.v_int32; break;
    case METRIC_UINT32: avg_r = v->value.v_uint32; break;
    case METRIC_INT64: avg_r = v->value.v_int64; break;
    case METRIC_UINT64: avg_r = v->value.v_uint64; break;
    case METRIC_DOUBLE: avg_r = v->value.v_double; break;
    default: avg_r = 0.0;
  }

  /* calculate new average */
  if(count) {
    avg = (((double)a_count) * avg_l) + (((double)v_count) * avg_r);
    avg /= (double)count;
  }

  if(count) {
    /* Set the value */
    if(avg == (double)((uint64_t)avg)) {
      a->type = METRIC_UINT64;
      a->value.v_uint64 = avg;
    }
    else if(avg == (double)((int64_t)avg)) {
      a->type = METRIC_INT64;
      a->value.v_int64 = avg;
    }
    else {
      a->type = METRIC_DOUBLE;
      a->value.v_double = avg;
    }

    /* calc the stddev */
    if(a->stddev_present || v->stddev_present) {
      if(!a->stddev_present) {
        a->stddev = 0;
      }
      a->stddev_present = 1;

      if(v->stddev_present) {
        /* both are present, combine the populations */
        /* N: cardinality, A: average, S: stddev, a: left, v: right, c: comb */
        /* (((Na * (Sa^2 + Aa^2) + Nv * (Sv^2 + Av^2)) / (Nc)) - Ac^2)^0.5 */
        stddev = ((double)a_count) * ((a->stddev*a->stddev) + (avg_l*avg_l))
               + ((double)v_count) * ((v->stddev*v->stddev) + (avg_r*avg_r));
        stddev /= (double)count;
        stddev -= (avg*avg);
        a->stddev = sqrt(stddev);
      }
    }
  }

  if(a_drun + v_drun) {
    /* Store these for a future calculation of stddev */
    avg_l = a->derivative;
    avg_r = v->derivative;

    /* Handle the derivative */
    if(isnanf(a->derivative) || a_drun == 0) {
      a->derivative = v->derivative;
    }
    else if(!isnanf(v->derivative)) {
      a->derivative = ((double)a_drun * a->derivative) + ((double)v_drun * v->derivative);
      a->derivative /= (double)(a_drun + v_drun);
    }

    if(isnanf(a->derivative_stddev) || a_drun == 0) {
      a->derivative_stddev = v->derivative_stddev;
    }
    else if(!isnanf(v->derivative_stddev)) {
      stddev = ((double)a_drun * ((a->derivative_stddev * a->derivative_stddev) + avg_l*avg_l))
             + ((double)v_drun * ((v->derivative_stddev * v->derivative_stddev) + avg_r*avg_r));
      stddev /= (double)(a_drun + v_drun);
      stddev -= (a->derivative * a->derivative);
      a->derivative_stddev = sqrt(stddev);
    }
  }

  if(a_crun + v_crun) {
    /* Store these for a future calculation of stddev */
    avg_l = a->counter;
    avg_r = v->counter;

    /* Handle the counter */
    if(isnanf(a->counter) || a_crun == 0) {
      a->counter = v->counter;
    }
    else if(!isnanf(v->counter)) {
      a->counter = ((double)a_crun * a->counter) + ((double)v_crun * v->counter);
      a->counter /= (double)(a_crun + v_crun);
    }
    /* Handle the stddev */
    if(isnanf(a->counter_stddev) || a_crun == 0) {
      a->counter_stddev = v->counter_stddev;
    }
    else if(!isnanf(v->counter_stddev)) {
      stddev = ((double)a_crun * ((a->counter_stddev * a->counter_stddev) + avg_l*avg_l))
             + ((double)v_crun * ((v->counter_stddev * v->counter_stddev) + avg_r*avg_r));
      stddev /= (double)(a_crun + v_crun);
      stddev -= (a->counter * a->counter);
      a->counter_stddev = sqrt(stddev);
    }
  }

  /* special case to up-coerce an absent to a NULL */
  if(a->type == METRIC_ABSENT && v->type == METRIC_NULL)
    a->type = METRIC_NULL;

  return count;
}

void
noit_metric_rollup_accumulate_numeric(noit_numeric_rollup_accu* accu, noit_metric_value_t* value) {
  /*int lv;
     uuid_t id;
     char uuid_str[UUID_STR_LEN+1];*/
    noit_metric_value_t last_value = accu->last_value;
    accu->last_value = *value;

    nnt_multitype *w1 = &accu->accumulated;
    int *w1_drun, *w1_crun/*, *w5_drun, *w5_crun*/;

    /* get the actual datum to update */
    w1_drun = &accu->drun;
    w1_crun = &accu->crun;

    if (accu->accumulated.type != METRIC_NULL
        && accu->first_value_time_ms >= value->whence_ms) {
      /* It's older! */
      return;
    } else if (accu->accumulated.type != METRIC_NULL) {
      /* here we have last_value and value */
      /* Handle the numeric case */
      int drun = 0;
      double dy, derivative = private_nan;
      nnt_multitype current;

      if (value->type == METRIC_NULL
          || value->type == METRIC_ABSENT)
        return;

      /* set derivative and drun */
      calculate_change(&last_value, value, &dy, &drun);
      if (drun > 0) {
        derivative = (1000.0 * dy) / (double) drun;
      }

      /* setup a faux nnt_multitype so we can accum */
      memset(&current, 0, sizeof(current));
      memcpy(&current.value, &value->value, sizeof(current.value));
      current.count = 1;
      current.type = value->type;
      current.stddev_present = 1;
      current.derivative = derivative;
      if (derivative >= 0)
        current.counter = derivative;
      else
        current.counter = private_nan; /* NaN */
      nnt_multitype_accum_counts(w1, w1->count, *w1_drun, *w1_crun, &current,
          1, drun, derivative >= 0 ? drun : 0);
      *w1_drun += drun;
      if (derivative >= 0) {
        *w1_crun += drun;
      }
      /* We've added one data point */
      w1->count++;
    } else { // values.type!==METRIC_NULL
      /* Handle the case where this is the first value */
      w1->type = value->type;
      w1->count = 1;
      accu->first_value_time_ms = value->whence_ms;
      switch (value->type) {
      case METRIC_NULL:
      case METRIC_ABSENT:
        w1->count = 0;
        break;
      case METRIC_STRING:
        exit(1);
        break; // break without effect but used to get rid of gcc warning message
      default: // This will copy all 64 bits and hence works for every type
        w1->value.v_uint64 = value->value.v_uint64;
        break;
      }
    }
}
