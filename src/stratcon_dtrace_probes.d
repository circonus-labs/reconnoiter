provider stratcon {
  probe reschedule (int, char *, char *, char *, int);
  probe shutdown__permanent (int, char *, char *, char *);
  probe connect (int, char *, char *, char *);
  probe connect__success (int, char *, char *, char *);
  probe connect__close (int, char *, char *, char *, int, int);
  probe connect__failed (int, char *, char *, char *, int);
  probe connect__ssl (int, char *, char *, char *);
  probe connect__ssl__success (int, char *, char *, char *);
  probe connect__ssl__failed (int, char *, char *, char *, char *, int);
  probe stream__count (int, char *, char *, char *, int);
  probe stream__header(int, char *, char *, char *, int, int, int, int, int);
  probe stream__body(int, char *, char *, char *, int, int, int, int, char *);
  probe stream__checkpoint(int, char *, char *, char *, int, int);
};
provider noit_log {
  probe log (char *, char *, int, char *);
};
provider eventer {
  probe callback__entry (void *, char *, int, int, int);
  probe callback__return (void *, char *, int);
};
