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

CREATE SEQUENCE event_criteria_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

GRANT SELECT,UPDATE ON SEQUENCE event_criteria_id_seq TO stratcon;

CREATE TABLE event_criteria_numeric (
  check_id uuid not null,
  metric_name text not null,
  event_criteria_id integer not null primary key,
  derive text check (derive in ('derive_5m','counter_5m',NULL)) 
);

CREATE INDEX event_criteria_numeric_check_id_idx
    ON event_criteria_numeric (check_id);

CREATE TABLE event_criteria_text (
  check_id uuid not null,
  metric_name text not null,
  event_criteria_id integer not null primary key 
);

CREATE INDEX event_criteria_text_check_id_idx
    ON event_criteria_text (check_id);

GRANT SELECT ON TABLE event_criteria_numeric TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_numeric TO stratcon;

GRANT SELECT ON TABLE event_criteria_text TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_text TO stratcon;

