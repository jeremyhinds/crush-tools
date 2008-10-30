/********************************
   Copyright 2008 Google Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 ********************************/
#include <dbfr.h>

#include "aggregate_main.h"
#include "aggregate.h"

#define AGG_TMP_BUF_SIZE 64

int ncounts;                    /* the number of count fields */
int nsums;                      /* the number of sum fields */
int naverages;                  /* the number of average fields */
int *sum_precisions;            /* array of precisions for the sum fields */
int *average_precisions;        /* array of precisions for the average fields */
char *delim;


/** @brief  
  * 
  * @param args contains the parsed cmd-line options & arguments.
  * @param argc number of cmd-line arguments.
  * @param argv list of cmd-line arguments
  * @param optind index of the first non-option cmd-line argument.
  * 
  * @return exit status for main() to return.
  */
int aggregate(struct cmdargs *args, int argc, char *argv[], int optind) {

  int i, n;

  hashtbl_t aggregations;
  llist_t *hash_keys;

  int nkeys;                    /* the number of key fields */
  int *key_fields;              /* array of key field indexes */
  int *count_fields;            /* array of count field indexes */
  int *sum_fields;              /* array of sum field indexes */
  int *average_fields;          /* array of average field indexes */
  size_t arsz = 0;              /* size of array allocated by expand_nums() */

  size_t n_hash_elems;

  FILE *in;                     /* input file */
  dbfr_t *in_reader;

  char *outbuf;                 /* buffer for a line of output */
  size_t outbuf_sz;             /* size of the output buffer */

  char default_delim[] = { 0xFE, 0x00 };  /* default delimiter string */

  if (! args->keys && ! args->key_labels) {
    fprintf(stderr, "%s: -k or -K must be specified\n", argv[0]);
    return EXIT_HELP;
  }

  delim = args->delim;
  if (!delim)
    delim = getenv("DELIMITER");

  if (delim)
    expand_chars(delim);
  else
    delim = default_delim;

  if (optind == argc)
    in = stdin;
  else
    in = nextfile(argc, argv, &optind, "r");

  if (in == NULL)
    return EXIT_FILE_ERR;

  in_reader = dbfr_init(in);

  nkeys = ncounts = nsums = naverages = 0;
  key_fields = count_fields = sum_fields = average_fields = NULL;

  arsz = 0;
  if (args->keys) {
    nkeys = expand_nums(args->keys, &key_fields, &arsz);
  } else if (args->key_labels) {
    nkeys = expand_label_list(args->key_labels, in_reader->next_line,
                              delim, &key_fields, &arsz);
    args->preserve = 1;
  }

  if (nkeys <= 0) {
    fprintf(stderr, "%s: error expanding key fields\n", argv[0]);
    return EXIT_HELP;
  }

  decrement_values(key_fields, nkeys);

  arsz = 0;
  if (args->sums) {
    nsums = expand_nums(args->sums, &sum_fields, &arsz);
  } else if (args->sum_labels) {
    nsums = expand_label_list(args->sum_labels, in_reader->next_line,
                              delim, &sum_fields, &arsz);
    args->preserve = 1;
  }
  if (nsums < 0) {
    fprintf(stderr, "%s: error expanding sum-field list\n", argv[0]);
    return EXIT_HELP;
  } else if (nsums > 0) {
    decrement_values(sum_fields, nsums);
    sum_precisions = malloc(sizeof(int) * nsums);
    memset(sum_precisions, 0, sizeof(int) * nsums);
  }

  arsz = 0;
  if (args->counts) {
    ncounts = expand_nums(args->counts, &count_fields, &arsz);
  } else if (args->count_labels) {
    ncounts = expand_label_list(args->count_labels, in_reader->next_line,
                                delim, &count_fields, &arsz);
    args->preserve = 1;
  }
  if (ncounts < 0) {
    fprintf(stderr, "%s: error expanding count-field list\n", argv[0]);
    return EXIT_HELP;
  } else if (ncounts > 0) {
    decrement_values(count_fields, ncounts);
  }

  arsz = 0;
  if (args->averages) {
    naverages = expand_nums(args->averages, &average_fields, &arsz);
  } else if (args->average_labels) {
    naverages = expand_label_list(args->average_labels, in_reader->next_line,
                                  delim, &average_fields, &arsz);
    args->preserve = 1;
  }

  if (naverages < 0) {
    fprintf(stderr, "%s: error expanding average-field list\n", argv[0]);
    return EXIT_HELP;
  } else if (naverages > 0) {
    decrement_values(average_fields, naverages);
    average_precisions = malloc(sizeof(int) * naverages);
    memset(average_precisions, 0, sizeof(int) * naverages);
  }

#ifdef CRUSH_DEBUG
  fprintf(stderr, "%d keys: ", nkeys);
  for (i = 0; i < nkeys; i++)
    fprintf(stderr, "%d ", key_fields[i]);
  fprintf(stderr, "\n%d sums: ", nsums);
  for (i = 0; i < nsums; i++)
    fprintf(stderr, "%d ", sum_fields[i]);
  fprintf(stderr, "\n%d averages: ", naverages);
  for (i = 0; i < naverages; i++)
    fprintf(stderr, "%d ", average_fields[i]);
  fprintf(stderr, "\n%d counts: ", ncounts);
  for (i = 0; i < ncounts; i++)
    fprintf(stderr, "%d ", count_fields[i]);
  fprintf(stderr, "\n\n");
#endif

  outbuf = NULL;
  outbuf_sz = 0;

  /* set locale with values from the environment so strcoll()
     will work correctly. */
  setlocale(LC_ALL, "");
  setlocale(LC_COLLATE, "");

  if (args->preserve) {
    size_t str_len;

    if (dbfr_getline(in_reader) <= 0) {
      fprintf(stderr, "%s: unexpected end of file\n", getenv("_"));
      exit(EXIT_FILE_ERR);
    }
    chomp(in_reader->current_line);

    outbuf = malloc(in_reader->current_line_len);
    if (!outbuf) {
      fprintf(stderr, "%s: out of memory.\n", getenv("_"));
      exit(EXIT_MEM_ERR);
    }
    outbuf_sz = in_reader->current_line_len;

    extract_fields_to_string(in_reader->current_line, outbuf, outbuf_sz,
                             key_fields, nkeys, delim, NULL);
    fputs(outbuf, stdout);
    if (args->labels) {
    	printf("%s%s", delim, args->labels);
    } else {
      if (nsums) {
        extract_fields_to_string(in_reader->current_line, outbuf, outbuf_sz,
                                 sum_fields, nsums, delim,
                                 args->auto_label ? "-Sum" : NULL);
        printf("%s%s", delim, outbuf);
      }

      if (ncounts) {
        extract_fields_to_string(in_reader->current_line, outbuf, outbuf_sz,
                                 count_fields, ncounts, delim,
                                 args->auto_label ? "-Count" : NULL);
        printf("%s%s", delim, outbuf);
      }

      if (naverages) {
        extract_fields_to_string(in_reader->current_line, outbuf, outbuf_sz,
                                 average_fields, naverages, delim,
                                 args->auto_label ? "-Average" : NULL);
        printf("%s%s", delim, outbuf);
      }
    }
    fputs("\n", stdout);

  }

  ht_init(&aggregations, 1024, NULL, (void (*)) free_agg);
  /* ht_init( &aggregations, 1024, NULL, free ); */

  n_hash_elems = 0;

  /* loop through all files */
  while (in != NULL) {
    struct aggregation *value;
    char tmpbuf[AGG_TMP_BUF_SIZE];
    size_t tmplen;
    int in_hash;

    /* loop through each line of the file */
    while (dbfr_getline(in_reader) > 0) {
      chomp(in_reader->current_line);
      if (in_reader->current_line_len > outbuf_sz) {
        char *tmp_outbuf = realloc(outbuf, in_reader->current_line_len + 32);
        if (!tmp_outbuf) {
          fprintf(stderr, "%s: out of memory.\n", getenv("_"));
          goto aggregate_cleanup;
        }
        outbuf = tmp_outbuf;
        outbuf_sz = in_reader->current_line_len + 32;
      }

      extract_fields_to_string(in_reader->current_line, outbuf, outbuf_sz,
                               key_fields, nkeys, delim, NULL);

      value = (struct aggregation *) ht_get(&aggregations, outbuf);
      if (!value) {
        in_hash = 0;
        value = alloc_agg(nsums, ncounts, naverages);
        /* value = malloc(sizeof(struct aggregation));
           memset(value, 0, sizeof(struct aggregation)); */
        if (!value) {
          fprintf(stderr, "%s: out of memory.\n", getenv("_"));
          goto aggregate_cleanup;
        }
      } else {
        in_hash = 1;
      }

      /* sums */
      for (i = 0; i < nsums; i++) {
        tmplen =
          get_line_field(tmpbuf, in_reader->current_line,
                         AGG_TMP_BUF_SIZE - 1, sum_fields[i], delim);
        if (tmplen > 0) {
          n = float_str_precision(tmpbuf);
          if (sum_precisions[i] < n)
            sum_precisions[i] = n;
          value->sums[i] += atof(tmpbuf);
        }
      }

      /* averages */
      for (i = 0; i < naverages; i++) {
        tmplen =
          get_line_field(tmpbuf, in_reader->current_line,
                         AGG_TMP_BUF_SIZE - 1, average_fields[i], delim);
        if (tmplen > 0) {
          n = float_str_precision(tmpbuf);
          if (average_precisions[i] < n)
            average_precisions[i] = n;
          value->average_sums[i] += atof(tmpbuf);
          value->average_counts[i] += 1;
        }
      }

      /* counts */
      for (i = 0; i < ncounts; i++) {
        tmplen =
          get_line_field(tmpbuf, in_reader->current_line,
                         AGG_TMP_BUF_SIZE - 1, count_fields[i], delim);
        if (tmplen > 0) {
          value->counts[i] += 1;
        }
      }

      if (!in_hash) {
        if (ht_put(&aggregations, outbuf, value) != 0)
          fprintf(stderr, "%s: failed to store value in hashtable.\n",
                  getenv("_"));
        n_hash_elems++;
      }

    }
    dbfr_close(in_reader);
    in = nextfile(argc, argv, &optind, "r");
    if (in) {
      in_reader = dbfr_init(in);
    }
  }

  /* print all of the output */
  if (args->nosort) {
    /* it will be a little faster if the user indicates that
       the output sort order doesn't matter */
    for (i = 0; i < aggregations.arrsz; i++) {
      hash_keys = aggregations.arr[i];
      if (hash_keys != NULL) {
        ll_call_for_each(hash_keys, ht_print_keys_sums_counts_avgs);
      }
    }
  } else {
    llist_node_t *node;
    struct aggregation *val;
    char **key_array;
    int j = 0;
    key_array = malloc(sizeof(char *) * n_hash_elems);

    /* put all the keys into an array */
    for (i = 0; i < aggregations.arrsz; i++) {
      hash_keys = aggregations.arr[i];
      if (hash_keys != NULL) {
        for (node = hash_keys->head; node; node = node->next) {
          key_array[j] = ((ht_elem_t *) node->data)->key;
          j++;
        }
      }
    }

    /* sort the keys */
    qsort(key_array, n_hash_elems, sizeof(char *),
          (int (*)(const void *, const void *)) key_strcmp);
    /* (int (*)(const void *, const void *))strcmp ); */

    /* print everything out */
    for (i = 0; i < n_hash_elems; i++) {
      val = (struct aggregation *) ht_get(&aggregations, key_array[i]);
      print_keys_sums_counts_avgs(key_array[i], val);
    }

    free(key_array);
  }


aggregate_cleanup:
  ht_destroy(&aggregations);

  if (key_fields)
    free(key_fields);
  if (sum_fields)
    free(sum_fields);
  if (count_fields)
    free(count_fields);
  if (outbuf)
    free(outbuf);

  return EXIT_OKAY;
}

int key_strcmp(char **a, char **b) {
  char fa[256], fb[256];
  int retval = 0;
  int i;
  size_t alen, blen;

  /* avoid comparing nulls */
  if (!*a && !*b)
    return 0;
  if (!*a && *b)
    return -1;
  if (*a && !*b)
    return 1;

  i = 0;
  while (retval == 0) {
    alen = get_line_field(fa, *a, 255, i, delim);
    blen = get_line_field(fb, *b, 255, i, delim);
    if (alen < 0 || blen < 0)
      break;
    retval = strcoll(fa, fb);
    i++;
  }

  return retval;
}

int float_str_precision(char *d) {
  char *p;
  int after_dot;
  if (!d)
    return 0;

  p = strchr(d, '.');
  if (p == NULL) {
    return 0;
  }
  after_dot = p - d + 1;
  return (strlen(d) - after_dot);
}

int print_keys_sums_counts_avgs(char *key, struct aggregation *val) {
  int i;
  fputs(key, stdout);
  for (i = 0; i < nsums; i++) {
    printf("%s%.*f", delim, sum_precisions[i], val->sums[i]);
  }
  for (i = 0; i < ncounts; i++) {
    printf("%s%d", delim, val->counts[i]);
  }
  for (i = 0; i < naverages; i++) {
    printf("%s%.*f", delim, average_precisions[i] + 2,
           val->average_sums[i] / val->average_counts[i]);
  }
  fputs("\n", stdout);
  return 0;
}

int ht_print_keys_sums_counts_avgs(void *htelem) {
  char *key;
  struct aggregation *val;
  key = ((ht_elem_t *) htelem)->key;
  val = ((ht_elem_t *) htelem)->data;
  return print_keys_sums_counts_avgs(key, val);
}

void extract_fields_to_string(char *line, char *destbuf, size_t destbuf_sz,
                              int *fields, size_t nfields, char *delim,
                              char *suffix) {
  char *pos;
  int i;
  size_t delim_len = 0, field_len = 0, suffix_len = 0;

  delim_len = strlen(delim);
  if (suffix)
    suffix_len = strlen(suffix);
  pos = destbuf;

  for (i = 0; i < nfields; i++) {
    field_len =
        get_line_field(pos, line, destbuf_sz - (pos - destbuf),
                       fields[i], delim);
    pos += field_len;
    if (suffix) {
    	strncat(pos, suffix, destbuf_sz - (pos - destbuf));
    	pos += suffix_len;
    }
    if (i != nfields - 1) {
    	strncat(pos, delim, destbuf_sz - (pos - destbuf));
      pos += delim_len;
    }
  }
}

void decrement_values(int *array, size_t sz) {
  int j;
  if (array == NULL || sz == 0)
    return;
  for (j = 0; j < sz; j++) {
    array[j]--;
  }
}

struct aggregation *alloc_agg(int nsum, int ncount, int naverage) {
  struct aggregation *agg;

  agg = malloc(sizeof(struct aggregation));
  if (!agg)
    goto alloc_agg_error;

  agg->counts = NULL;
  agg->sums = NULL;
  agg->average_sums = NULL;
  agg->average_counts = NULL;

  if (nsum > 0) {
    agg->sums = malloc(sizeof(double) * nsum);
    if (!agg->sums)
      goto alloc_agg_error;
    memset(agg->sums, 0, sizeof(double) * nsum);
  }

  if (ncount > 0) {
    agg->counts = malloc(sizeof(u_int32_t) * ncount);
    if (!agg->counts)
      goto alloc_agg_error;
    memset(agg->counts, 0, sizeof(u_int32_t) * ncount);
  }

  if (naverage > 0) {
    agg->average_sums = malloc(sizeof(double) * naverage);
    if (!agg->average_sums)
      goto alloc_agg_error;
    memset(agg->average_sums, 0, sizeof(double) * naverage);

    agg->average_counts = malloc(sizeof(u_int32_t) * naverage);
    if (!agg->average_counts)
      goto alloc_agg_error;
    memset(agg->average_counts, 0, sizeof(u_int32_t) * naverage);
  }

  return agg;

alloc_agg_error:
  if (agg) {
    if (agg->sums)
      free(agg->sums);
    if (agg->counts)
      free(agg->counts);
    if (agg->average_sums)
      free(agg->average_sums);
    if (agg->average_counts)
      free(agg->average_counts);
    free(agg);
  }
  return NULL;
}

void free_agg(struct aggregation *agg) {
  if (!agg)
    return;
  if (agg->counts)
    free(agg->counts);
  if (agg->sums)
    free(agg->sums);
  if (agg->average_sums)
    free(agg->average_sums);
  if (agg->average_counts)
    free(agg->average_counts);
  free(agg);
}
