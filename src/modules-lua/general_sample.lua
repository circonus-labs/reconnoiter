module(..., package.seeall)

function a(name, delay)
  for i=1,10 do
    noit.log("error", name .. " I am here\n")
    noit.sleep(delay)
  end
end

function handler()
  noit.coroutine_spawn(a, "(1)", 2)
  noit.coroutine_spawn(a, "(2)", 3)
end
