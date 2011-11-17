provider eventer {
  probe callback__entry (void *, char *, int, int, int);
  probe callback__return (void *, char *, int);
};
