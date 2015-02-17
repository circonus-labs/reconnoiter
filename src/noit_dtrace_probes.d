provider noit {
  probe check__dispatch(char *, char *, char *, char *);
  probe check__metric(char *, char *, char *, char *, char *, int, char *);
  probe check__status(char *, char *, char *, char *, int, int, char *);
};
