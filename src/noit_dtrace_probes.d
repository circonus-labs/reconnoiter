provider noit {
  probe check__dispatch(char *, char *, char *, char *);
  probe check__metric(char *, char *, char *, char *, char *, int, char *);
  probe check__status(char *, char *, char *, char *, int, int, char *);
};

provider noit_log {
  probe log (char *, char *, int, char *);
};

provider eventer {
  probe callback__entry (void *, char *, int, int, int);
  probe callback__return (void *, char *, int);
};
