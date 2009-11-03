--
-- PostgreSQL database dump
--

SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

SET search_path = stratcon, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: event_criteria; Type: TABLE; Schema: stratcon; Owner: reconnoiter;
--

CREATE TABLE event_criteria_numeric (
  check_id uuid not null,
  metric_name text not null,
  ordering integer not null,
  event_criteria_id integer not null,
  priority integer not null,
  derive text,
  minimum numeric,
  maximum numeric,
  absence boolean not null default true
);

CREATE UNIQUE INDEX event_criteria_numeric_cmo
    ON event_criteria_numeric (check_id, metric_name, ordering);

CREATE INDEX event_criteria_numeric_cid
    ON event_criteria_numeric (event_criteria_id);

CREATE TABLE event_criteria_text (
  check_id uuid not null,
  metric_name text not null,
  ordering integer not null,
  event_criteria_id integer not null,
  priority integer not null,
  match text,
  onchange boolean not null default false,
  absence boolean not null default true
);

CREATE UNIQUE INDEX event_criteria_text_cmo
    ON event_criteria_text (check_id, metric_name, ordering);

CREATE UNIQUE INDEX event_criteria_text_cid
    ON event_criteria_text (event_criteria_id);

REVOKE ALL ON TABLE event_criteria_numeric FROM PUBLIC;
REVOKE ALL ON TABLE event_criteria_numeric FROM reconnoiter;
GRANT ALL ON TABLE event_criteria_numeric TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_numeric TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_numeric TO stratcon;

REVOKE ALL ON TABLE event_criteria_text FROM PUBLIC;
REVOKE ALL ON TABLE event_criteria_text FROM reconnoiter;
GRANT ALL ON TABLE event_criteria_text TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_text TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_text TO stratcon;

