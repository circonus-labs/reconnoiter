SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

SET search_path = stratcon, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;


CREATE TABLE login (
    Username varchar(40),
    Password varchar(40),
    E_mail varchar(40)
);


GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE login TO stratcon;
