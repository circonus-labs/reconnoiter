provider stratcon {
  probe stream__count (int, char *, char *, char *, int);
  probe stream__header(int, char *, char *, char *, int, int, int, int, int);
  probe stream__body(int, char *, char *, char *, int, int, int, int, char *);
  probe stream__checkpoint(int, char *, char *, char *, int, int);
};
