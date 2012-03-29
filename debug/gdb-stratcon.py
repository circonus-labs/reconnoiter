import gdb

watchpoints = {}

class http_closure_wp(gdb.Breakpoint):
    def __init__(self, spec):
        gdb.Breakpoint.__init__(self, spec, gdb.BP_WATCHPOINT)
        self.address = spec

    # Print out the stack here
    def stop (self):
         gdb.execute("bt")

         return True

# Set a watchpoint on alloc
class http_closure_alloc_bp(gdb.Breakpoint):
    def stop (self):
        frame = gdb.newest_frame()
        restc = gdb.parse_and_eval("restc")
        addr = str(restc)
        spec = restc['wants_shutdown'].address

        print("alloc of " + str(addr) + " setting watchpoint @ " + str(spec))
        watchpoints[addr] = http_closure_wp("* (int *)" + str(spec))

        return False

# Delete the watchpoint on free
class http_closure_free_bp(gdb.Breakpoint):
    def stop (self):
        frame = gdb.newest_frame()
        restc = gdb.parse_and_eval("v")
        addr = str(restc)

        print("dealloc of " + str(addr) + " deleting watchpoint")
        gdb.execute("bt")
        wp = watchpoints[addr]
        wp.delete()
        del watchpoints[addr]

        return False

http_closure_alloc_bp("noit_rest.c:263")
http_closure_free_bp("noit_rest.c:284")
