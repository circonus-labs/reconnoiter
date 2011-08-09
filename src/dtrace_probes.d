provider stratcon_noit {
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

provider noit_check {
  probe dispatch(char *, char *, char *, char *);
  probe status(char *, char *, char *, char *, int, int, char *);
  probe metric(char *, char *, char *, char *, char *, int, char *);
};
