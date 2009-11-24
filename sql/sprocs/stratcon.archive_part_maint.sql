CREATE OR REPLACE FUNCTION stratcon.archive_part_maint(in_parent_table text, in_column text, in_units text, in_cnt integer) RETURNS void 
AS $$
DECLARE
    v_recent_part date;
    v_schema_name text;
    v_table_name text;
    v_constraint_name text;
    v_next_part date;
    v_parent_table text;
    v_rec record;
    v_sql text;
    v_has_perms boolean;
BEGIN
    v_parent_table := substring(in_parent_table from E'\\.(.+)');
    IF v_parent_table IS NULL THEN
      v_parent_table := in_parent_table;
    END IF;
    v_schema_name := substring(in_parent_table from E'^([^.]+)');
    IF v_schema_name IS NULL THEN
      v_schema_name := 'stratcon';
    END IF;

    select d into v_recent_part from (
        select overlay(
                   overlay(
                       substring(relname from E'_([0-9]{8})$')
                       placing '-' from 7 for 0)
                   placing '-' from 5 for 0)::date as d from pg_class
         where oid in (select inhrelid
                         from pg_inherits as i
                         join pg_class as c on(i.inhparent = c.oid)
                         join pg_namespace as n on (c.relnamespace = n.oid)
                        where nspname = v_schema_name
                          and relname = v_parent_table)) as x
    order by d desc
    limit 1;

    IF v_recent_part IS NULL THEN
        select (date_trunc(in_units, current_date) - ('1 '||in_units)::interval)::date
          into v_recent_part;
    END IF;

    select date_trunc(in_units, v_recent_part + ('1 '||in_units)::interval)::date
      into v_next_part;

    LOOP
        IF v_next_part > current_date + (in_cnt * ('1 '||in_units)::interval) THEN
            EXIT;
        END IF;
        v_table_name := v_parent_table || '_' || extract(YEAR from v_next_part) || 
                        lpad(extract(month from v_next_part)::text, 2, '0') ||
                        lpad(extract(day from v_next_part)::text, 2, '0');
        v_constraint_name := 'check_' || v_table_name;

        execute 'CREATE TABLE ' || v_schema_name || '.' || v_table_name || '(' ||
                'CONSTRAINT ' || v_constraint_name ||
                ' CHECK (' || in_column || E' >= \'' || v_next_part::text || E' 00:00:00-00\'::timestamptz AND ' ||
                '        ' || in_column || E' < \'' || (v_next_part + ('1 '||in_units)::interval)::date::text || E' 00:00:00-00\'::timestamptz)' ||
                ') INHERITS (' || in_parent_table || ')';

        RAISE INFO 'created partition %', v_table_name;

        FOR v_rec in
            select replace(indexdef, v_parent_table, v_table_name) as sql
              from pg_indexes
             where tablename = v_parent_table and schemaname = v_schema_name
        LOOP
          RAISE INFO 'recreated parent indexes on %', v_table_name;
          execute v_rec.sql;
        END LOOP;

        -- no public access
        select count(*) > 0 into v_has_perms
          from information_schema.table_privileges
         where table_schema=v_schema_name and table_name=v_parent_table;

        IF v_has_perms THEN
          execute 'REVOKE ALL ON ' || v_schema_name || '.' || v_table_name || ' FROM PUBLIC';
        END IF;

        FOR v_rec in
            select 'GRANT ' || privilege_type || ' ON ' || v_schema_name || '.' || v_table_name ||
                   ' TO ' || grantee as sql
              from information_schema.table_privileges
             where table_schema=v_schema_name and table_name=v_parent_table
        LOOP
          execute v_rec.sql;
        END LOOP;
        RAISE INFO 'set permissions on partition %', v_table_name;

        FOR v_rec in
            select tgname, tgtype, nspname, proname
              from pg_class as c join pg_trigger as t on(c.oid = t.tgrelid)
              join pg_proc as p on(t.tgfoid = p.oid)
              join pg_namespace as n on(p.pronamespace = n.oid) 
             where relname = v_parent_table
               and relnamespace in (select oid from pg_namespace
                                     where nspname = v_schema_name)
               and proname <> 'parent_empty' LOOP
          RAISE INFO 'creating trigger % on partition %', v_rec.tgname, v_table_name;
          v_sql := 'CREATE TRIGGER ' || v_rec.tgname || '';
          IF 0 != (v_rec.tgtype & 2) THEN
            v_sql := v_sql || ' BEFORE ';
          ELSE
            v_sql := v_sql || ' AFTER ';
          END IF;
          IF 0 != (v_rec.tgtype & 4) THEN
            v_sql := v_sql || ' INSERT ';
          END IF;
          IF 0 != (v_rec.tgtype & 8) THEN
            IF 0 != (v_rec.tgtype & 4) THEN
              v_sql := v_sql || ' OR ';
            END IF;
            v_sql := v_sql || ' DELETE ';
          END IF;
          IF 0 != (v_rec.tgtype & 16) THEN
            IF 0 != (v_rec.tgtype & 12) THEN
              v_sql := v_sql || ' OR ';
            END IF;
            v_sql := v_sql || ' UPDATE ';
          END IF;
          v_sql := v_sql || ' ON ' || v_schema_name || '.' || v_table_name;
          IF 0 != (v_rec.tgtype & 1) THEN
            v_sql := v_sql || ' FOR EACH ROW ';
          ELSE
            v_sql := v_sql || ' FOR EACH STATEMENT ';
          END IF;
          v_sql := v_sql || ' EXECUTE PROCEDURE ' || v_rec.nspname || '.' || v_rec.proname || '()';
          execute v_sql;
        END LOOP;

        v_next_part := (v_next_part + ('1 '||in_units)::interval)::date;
    END LOOP;
END
$$ LANGUAGE plpgsql;

