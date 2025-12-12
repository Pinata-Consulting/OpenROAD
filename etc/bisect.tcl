# Read in the ODB database and delete specified instances and nets
#
# This is used as part of a bisction algorithm to isolate
# problematic instances/nets

read_db $::env(ODB_FILE)

set block [[[ord::get_db] getChip] getBlock]
set nets [$block getNets]
set insts [$block getInsts]

# Define a procedure to clear the "do not touch" property for an instance
proc clear_dont_touch_inst { inst } {
  $inst setDoNotTouch false
  foreach iterm [$inst getITerms] {
    set net [$iterm getNet]
    if { $net ne "" } {
      $net setDoNotTouch false
    }
  }
}

# Define a procedure to clear the "do not touch" property for a net
proc clear_dont_touch_net { net } {
  $net setDoNotTouch 0
  foreach iterm [$net getITerms] {
    set inst [$iterm getInst]
    if { $inst ne "" } {
      $inst setDoNotTouch 0
    }
  }
}

set yaml_data [yaml::read_file $yaml_file]
set insts_to_delete [yaml::get $yaml_data instances]
set instances {}
foreach elm_idx $insts_to_delete {
  lappend instances [lindex $insts $elm_idx]
}
set nets_to_delete [yaml::get $yaml_data nets]
set nets {}
foreach elm_idx $nets_to_delete {
  lappend nets [lindex $nets $elm_idx]
}

foreach elm $instances {
  clear_dont_touch_inst $elm
  $elm destroy
}

foreach elm $nets {
  clear_dont_touch_net $elm
  $elm destroy
}

write_db $new_db
