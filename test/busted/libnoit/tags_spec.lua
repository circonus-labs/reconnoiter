local system = run_command_synchronously_return_output
describe("tags", function()
  it("should run test_tags", function()
    local rv, out, err = system({ env = { "LD_PRELOAD=../../src/libnoit.so" }, argv = { "../test_tags" } })
    if rv ~= 0 then
      print(out) print(err)
    end
    assert.is.equal(0, rv)
  end)
end)
