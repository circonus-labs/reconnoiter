SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;



SET default_tablespace = '';

SET default_with_oids = false;


CREATE TABLE login.register (
    Username varchar(40),
    Password varchar(150),
    E_mail varchar(50)
);

GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE login.register TO login;
