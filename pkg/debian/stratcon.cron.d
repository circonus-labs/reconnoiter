#rollup jobs
* * * * * postgres /usr/bin/psql -d reconnoiter -U stratcon -c "select stratcon.rollup_metric_numeric(rollup) from metric_numeric_rollup_config order by seconds asc;" >/tmp/rollup.log 2>&1
#cleanup jobs
01 00 * * * postgres /usr/bin/psql -d reconnoiter -U reconnoiter -c "select stratcon.archive_part_maint('noit.metric_numeric_archive', 'whence', 'day', 7);" 1>/dev/null
01 00 * * * postgres /usr/bin/psql -d reconnoiter -U reconnoiter -c "select stratcon.archive_part_maint('noit.metric_text_archive', 'whence', 'day', 7);" 1>/dev/null
01 00 * * * postgres /usr/bin/psql -d reconnoiter -U reconnoiter -c "select stratcon.archive_part_maint('noit.check_status_archive', 'whence', 'day', 7);" 1>/dev/null
01 00 * * * postgres /usr/bin/psql -d reconnoiter -U reconnoiter -c "select stratcon.archive_part_maint('noit.check_archive', 'whence', 'day', 7);" 1>/dev/null
01 00 * * * postgres /usr/bin/psql -d reconnoiter -U reconnoiter -c "delete from prism.saved_graphs where saved = false and last_update<current_timestamp - '1 day' ::interval;" 1>/dev/null
