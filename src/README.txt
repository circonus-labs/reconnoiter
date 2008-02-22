= Configuration File =

=== Sample Heirarchy ===

  <noit>
    <eventer/>
    <modules>
      <...>
        <module />
      </...>
    </modules>
    <logs>
      <...>
        <log>
          <outlet />
          <outlet />
        </log>
      </...>
    </logs>
    <listeners>
      <...>
        <listener/>
        <listener>
          <config />
          <sslconfig />
        <listener>
      </...>
    </listeners>
    <checks>
      <...>
        <check uuid="xxx" />
      </...>
    </checks>
  </noit>

Unless otherwise specified, elements and attributes are inherited from
all anscestors.  As is typical with inheritence, the youngest/deepest
value overrides values of anscestors.  If the value requests is a set
of key/value pairs, the the top-most anscestor is is queried, then its
child is queries merging values replacing conflicts and so on until
the youngest/deepest node is reached.

=== Example: ===

      <a foo="bar">
        <config>
          <key1>val a 1</key1>
          <key2>val a 2</key2>
        </config>
        <b quux="baz">
          <config>
            <key1>val b 1</key1>
            <key3>val b 3</key3>
          </config>
          <c foo="meme" />
        </b>
      </a>

When looking at the "foo" attribute we see the following values at nodes:
  a: bar
  b: bar
  c: meme

When looking at the "quux" attribute we see the following values at nodes:
  a: (none)
  b: baz
  c: baz

When looking at the key/value set "config" we see the following values at nodes:
  a: { key1: "val a 1", key2: "val a 2" }
  b: { key1: "val b 1", key2: "val a 2", key3: "val b 3" }
  c: { key1: "val b 1", key2: "val a 2", key3: "val b 3" }  (same as b)

This inheritance model allows for "non-repetitive" configuration
approaches: "express yourself once and reuse."


== Seciton <eventer>: ==

This section provides configuration directives to the eventing
subsystem.  Think of the eventer as the engine that drives all I/O,
jobs, and timed events.  To choose the eventer, the "implementation"
attribute is used in the eventer node.  For example, to select the
kqueue eventer (for Mac OS X or modern BSDs):

  <eventer implementation="kqueue" />

== Section <logs>: ==

The logs section contains <log> elements at arbitrary depths.  A log
requires several attributes to be useful:

 * "name" this must me unique.  Several names have meaning within the
   product.  If you match that name, you control how that information
   is logged.

 * "type" this specified ths underlying log writing implementation.
   Valid options are "file" (the default if unspecified), "jlog" which
   is the a binary 'auto-rotating', consumable, multi-subscriber log
   format.

 * "path" the path to the log (interpreted by the "type"
   implementation).

 * "disabled" (boolean: <true|false>, default false) that dictates
   whether the log implmenation will actually write anything.  If set
   to true, the log will be silences and will not propagate input to
   any attached outlets.

Within a <log> element, zero, one or many <outlet> elements may be
present.  The outlet element must contain a "name" attribute (this
attribute is not inherited) that matches a *previously* declared <log>
by its "name" attribute.  Foreach <outlet> tag, a sink is created so
that any input sent to the containing <log> will additionally be
propagated to the <outlet>.

There is an implicit log named "stderr" that is attached to file
descriptor 2.

=== Special log names ===

 * "stderr" : opened to file descriptor two during log initialization.

 * "error" : destination for error messages.

 * "debug" : destination for debugging output.

 * "check" : When a check is altered in any way (including creation),
   the identifying attributes, includeing the uuid are logged to this
   facility.

   'C' TIMESTAMP UUID TARGET MODULE NAME

 * "status" : When a check status changes (either availability or
   state) and neither the new state nor the old state are "unknown" it
   is considered a state change for the check and the new availability
   and new state are logged to this facility.

   'S' TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE

 * "metrics" : Each time a check is completed, the various metrics
   that were observed by the check are logged to this facility.  The
   VALUE is always a string or [[null]] (never binary encoded/packed).
   The TYPE indicates the datatype the check intended when it was
   observed.

   'M' TIMESTAMP UUID NAME TYPE VALUE

   NAME is the name of the metric and the encouraged format for this
   is "name`subname`subsubname`etc"

   TYPES: 
     * i: INT32
     * I: UINT32
     * l: INT64
     * L: UINT64
     * n: DOUBLE
     * s: STRING

=== Example ===

  <logs>
    <console>
      <outlet name="stderr" />
      <log name="error" />
      <log name="debug" disabled="true" />
    </console>
    <log name="feed" type="jlog" path="/var/log/noitd.feed" />
    <feeds>
      <outlet name="feed" />
      <log name="check" />
      <log name="metrics" />
      <log name="status">
        <outlet name="error" />
      </log>
    </feeds>
  </logs>

 In the above example:

 * a <console> metagroup is created for the purpose of inheriting the
   "stderr" outlet.  The logs named "error" and "debug" are
   instantiated and inherit the "stderr" outlet.  However, "debug" is
   disabled, so no input the the "debug" log will be written anywhere.

 * a log named "feed" is create of type "jlog" writing to the
   "/var/log/noitd.feed" directory (jlogs paths are directories, where
   as "file" paths are filenames).

 * a <feeds> metagroup is created for the purpose of inheriting the
   "feed" outlet.  The logs "check," "metrics," and "status" are
   instantiated and will log via the "feed" outlet (all writing to the
   same jlog).  Additionally, the "status" feed is given an additional
   outlet named "error" so we will see inputs to status in both the
   "feed" jlog and on the console ("stderr").

