provider noit {
  probe reschedule (int, char *, char *, int);
  probe shutdown__permanent (int, char *, char *);
  probe connect (int, char *, char *);
  probe connect__success (int, char *, char *);
  probe connect__close (int, char *, char *, int, int);
  probe connect__failed (int, char *, char *, int);
  probe connect__ssl (int, char *, char *);
  probe connect__ssl__success (int, char *, char *);
  probe connect__ssl__failed (int, char *, char *, char *, int);
  probe check__dispatch(char *, char *, char *, char *);
  probe check__metric(char *, char *, char *, char *, char *, int, char *);
  probe check__status(char *, char *, char *, char *, int, int, char *);
};

provider noit_log {
  probe log (char *, char *, int, char *);
};

provider eventer {
  probe accept__entry (int, void *, int, int, void *);
  probe accept__return (int, void *, int, int, void *, int);
  probe read__entry (int, char *, size_t, int, void *);
  probe read__return (int, char *, size_t, int, void *, int);
  probe write__entry (int, char *, size_t, int, void *);
  probe write__return (int, char *, size_t, int, void *, int);
  probe close__entry (int, int, void *);
  probe close__return (int, int, void *, int);
  probe callback__entry (void *, char *, int, int, int);
  probe callback__return (void *, char *, int);
};
