@@
expression base, nmemb, compar;
@@
- qsort(base, nmemb, sizeof(*base), compar);
+ QSORT(base, nmemb, compar);

@@
expression base, nmemb, compar;
@@
- qsort(base, nmemb, sizeof(base[0]), compar);
+ QSORT(base, nmemb, compar);

@@
type T;
T *base;
expression nmemb, compar;
@@
- qsort(base, nmemb, sizeof(T), compar);
+ QSORT(base, nmemb, compar);
