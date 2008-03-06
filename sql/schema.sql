BEGIN;

CREATE USER stratcon;

CREATE SCHEMA stratcon;

CREATE TABLE stratcon.loading_dock_check (
  remote_address inet,
  whence timestamptz not null,
  id uuid not null,
  target text not null,
  module text not null,
  name text not null,
  PRIMARY KEY(id,whence)
);
CREATE TABLE stratcon.loading_dock_status (
  remote_address inet,
  whence timestamptz not null,
  id uuid not null,
  state char(1) not null,
  availability char(1) not null,
  duration integer not null,
  status text,
  PRIMARY KEY(id,whence)
);
-- There's so much data in these tables, it would
-- be crazy not to specify a date range. So...
-- whence is first in the PK.
CREATE TABLE stratcon.loading_dock_metric_numeric (
  remote_address inet,
  whence timestamptz not null,
  id uuid not null,
  name text not null,
  type char(1) not null,
  value numeric,
  PRIMARY KEY(whence,id,name)
);
CREATE TABLE stratcon.loading_dock_metric_text (
  remote_address inet,
  whence timestamptz not null,
  id uuid not null,
  name text not null,
  type char(1) not null,
  value text,
  PRIMARY KEY(whence,id,name)
);

GRANT USAGE ON SCHEMA stratcon TO stratcon;
GRANT INSERT ON stratcon.loading_dock_check TO stratcon;
GRANT INSERT ON stratcon.loading_dock_status TO stratcon;
GRANT INSERT ON stratcon.loading_dock_metric_numeric TO stratcon;
GRANT INSERT ON stratcon.loading_dock_metric_text TO stratcon;

COMMIT;
