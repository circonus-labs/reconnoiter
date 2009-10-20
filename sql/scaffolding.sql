create user reconnoiter with createuser;
create database reconnoiter with owner = reconnoiter;
create user stratcon with unencrypted password 'stratcon';
alter user stratcon set search_path to noit,public;
create user prism with unencrypted password 'prism';
alter user prism set search_path to noit,public;
\c reconnoiter reconnoiter;
begin;

create language plpgsql;
create schema noit;
create schema stratcon;
create schema prism;
grant usage on schema stratcon to stratcon;
grant usage on schema stratcon to prism;
grant usage on schema noit to stratcon;
grant usage on schema noit to prism;
grant usage on schema prism to prism;

\i sprocs/noit.date_hour.sql

\i tables/noit.check_status_changelog.sql
\i tables/noit.metric_numeric_rollup_12hours.sql
\i tables/noit.metric_numeric_rollup_20m.sql
\i tables/noit.metric_numeric_rollup_5m.sql
\i tables/noit.metric_numeric_rollup_60m.sql
\i tables/noit.metric_numeric_rollup_6hours.sql
\i tables/noit.metric_numeric_rollup_queue.sql
\i tables/noit.metric_text_changelog.sql
\i tables/noit.metric_text_currently.sql
\i tables/noit.tasklock.sql
\i tables/noit.tasklock_sequence.sql
\i tables/stratcon.current_node_config.sql
\i tables/stratcon.current_node_config_changelog.sql
\i tables/stratcon.storage_node.sql
\i tables/stratcon.map_uuid_to_sid.sql
\i tables/prism.graph_templates.sql

\i sprocs/noit.check_archive_log_changes.sql
\i sprocs/noit.check_status_archive_log_changes.sql
\i sprocs/noit.mark_metric_numeric_rollup_buffer.sql
\i sprocs/noit.metric_text_archive_log_changes.sql
\i sprocs/noit.update_metric_summary_fulltext.sql
\i sprocs/noit.update_mns_via_check_tag.sql
\i sprocs/noit.update_mns_via_metric_tag.sql
\i sprocs/noit.update_mns_via_self.sql
\i sprocs/stratcon.choose_window.sql
\i sprocs/stratcon.fetch_dataset.sql
\i sprocs/stratcon.fetch_varset.sql
\i sprocs/stratcon.get_storage_node_for_sid.sql
\i sprocs/stratcon.init_metric_numeric_rollup_5m.sql
\i sprocs/stratcon.map_uuid_to_sid.sql
\i sprocs/stratcon.metric_name_summary_tsvector.sql
\i sprocs/stratcon.window_robust_derive.sql
\i sprocs/stratcon.update_config.sql
\i sprocs/prism.add_graph_tags.sql
\i sprocs/prism.add_tags.sql
\i sprocs/prism.check_name_saved_graphs.sql
\i sprocs/prism.remove_graph_tags.sql
\i sprocs/prism.remove_tags.sql
\i sprocs/prism.saved_graphs_tsvector.sql
\i sprocs/prism.trig_update_tsvector_saved_graphs.sql

\i tables/noit.check_archive.sql
\i tables/noit.check_currently.sql
\i tables/noit.check_status_archive.sql
\i tables/noit.check_tag.sql
\i tables/noit.metric_name_summary.sql
\i tables/noit.metric_numeric_archive.sql
\i tables/noit.metric_tag.sql
\i tables/noit.metric_text_archive.sql
\i tables/prism.saved_graphs.sql
\i tables/prism.saved_graphs_dep.sql
\i tables/prism.saved_worksheets.sql
\i tables/prism.saved_worksheets_dep.sql

commit;
