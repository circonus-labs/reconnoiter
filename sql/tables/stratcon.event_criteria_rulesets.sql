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

CREATE TABLE event_criteria_numeric_rulesets (
  event_criteria_id integer not null references event_criteria_numeric on update cascade on delete restrict,
  minimum numeric,
  maximum numeric,
  absence boolean not null default true,
  ordering integer not null,
  priority integer not null,
  check (coalesce(minimum,maximum) IS NOT NULL),
  primary key (event_criteria_id, ordering)  
);


CREATE TABLE event_criteria_text_rulesets (
  event_criteria_id integer not null primary key references event_criteria_text on update cascade on delete restrict,
  match text,
  onchange boolean not null default false,
  absence boolean not null default true,
  ordering integer not null,
  priority integer not null, 
  primary key (event_criteria_id, ordering)  
);


GRANT SELECT ON TABLE event_criteria_numeric_rulesets TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_numeric_rulesets TO stratcon;

GRANT SELECT ON TABLE event_criteria_text_rulesets TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE event_criteria_text_rulesets TO stratcon;

