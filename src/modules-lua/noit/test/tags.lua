module(..., package.seeall)
function onload(image)
  image.xml_description('')
  return 0
end

function init(module)
  return 0
end

function config(module, options)
  return 0
end

function initiate(modules, check)
  local target_type
  local failed = false

  check.metric_int32("metric1|ST[env:prod,type:foo]", 1)
  check.metric_int32("metric2|ST[env:dev,type:foo]", 1)
  check.metric_int32("metric3|ST[env:prod,type:debug]", 1)
  check.metric_int32("metric4|ST[env:staging]", 1)
  check.metric_int32("metric5", 1)
  check.good()
  check.available()
end
