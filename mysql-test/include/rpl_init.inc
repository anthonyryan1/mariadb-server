# ==== Purpose ====
#
# Set up replication on several servers in a specified topology.
#
# By default, this script does the following:
#  - Creates the connections server_1, server_2, ..., server_N.
#  - Calls RESET MASTER, RESET SLAVE, USE test, CHANGE MASTER, START SLAVE.
#  - Sets the connection to server_1 before exiting.
# With $rpl_check_server_ids parameter, the script does the following:
#  - Verifies that @@server_id of all servers are different.
#
# ==== Usage ====
#
# 1. If you are going to use more than two servers, create
#    rpl_test.cnf with the following contents:
#
#    !include ../my.cnf
#    [mysqld.1]
#    log-slave-updates
#    [mysqld.2]
#    log-slave-updates
#    ...
#    [mysqld.N]
#    log-slave-updates
#
#    [ENV]
#    SERVER_MYPORT_3= @mysqld.3.port
#    SERVER_MYPORT_4= @mysqld.4.port
#    SERVER_MYPORT_5= @mysqld.5.port
#    ...
#    SERVER_MYPORT_N= @mysqld.N.port
#
#    (It is allowed, but not required, to configure SERVER_MYPORT_1
#    and SERVER_MYPORT_2 too. If these variables are not set, the
#    variables MASTER_MYPORT and SLAVE_MYPORT are used instead.
#    These variables are configured in the rpl_1slave_base.cnf,
#    that is used in the default my.cnf, which is used by the rpl suite.)
#
# 2. Execute the following near the top of the test:
#
#    [--let $rpl_server_count= 7]
#    --let $rpl_topology= 1->2->3->1->4, 2->5, 6->7
#    [--let $rpl_check_server_ids= 1]
#    [--let $rpl_skip_change_master= 1]
#    [--let $rpl_skip_start_slave= 1]
#    [--let $rpl_skip_reset_master_and_slave= 1]
#    [--let $rpl_debug= 1]
#    [--let $slave_timeout= NUMBER]
#    --source include/rpl_init.inc
#
#    Parameters:
#
#    $rpl_server_count
#      The number of servers to configure.  If this is not set, the largest
#      number in $rpl_topology will be used.
#
#    $rpl_topology
#      A comma-separated list of replication chain
#      specifications. Each replication chain specification has the
#      form S1->S2->...->Sn, where 1 <= S1,...Sn <= $rpl_server_count.
#      This file will configure S(i+1) to be a slave of S(i).  If you
#      want to specify the empty topology (no server replicates at
#      all), you have to set $rpl_topology=none.
#
#    $rpl_check_server_ids
#      If $rpl_check_server_ids is set, this script checks that the
#      @@server_id of all servers are different. This is normally
#      guaranteed by mtr, so it is only useful for debugging.
#
#    $rpl_skip_reset_master_and_slave
#      By default, this script issues RESET MASTER and RESET SLAVE
#      before CHANGE MASTER and START SLAVE.  RESET MASTER and RESET
#      SLAVE are suppressed if $rpl_skip_reset_master_and_slave is
#      set.
#
#    $rpl_skip_change_master
#      By default, this script issues CHANGE MASTER so that all slaves
#      are ready to run as specified by $rpl_topology. CHANGE MASTER
#      is suppressed if $rpl_skip_change_master is set.
#
#    $rpl_skip_start_slave
#      By default, this script issues START SLAVE on all slaves
#      specified by $rpl_topology. START SLAVE is suppressed if
#      $rpl_skip_change_master is set.
#
#    $rpl_debug
#      By default, this script only outputs a static text that says
#      that rpl_init.inc was invoked. If $rpl_debug is set, additional
#      debug info is printed. The debug info may be nondeterministic,
#      so no test case should be checked in with $rpl_debug set.
#
#    $slave_timeout
#      Timeout used when waiting for the slave threads to start.
#      See include/wait_for_slave_param.inc
#
#
# ==== Side effects ====
#
# Changes current connection to server_1.

--source include/have_log_bin.inc


--let $include_filename= rpl_init.inc [topology=$rpl_topology]
--source include/begin_include_file.inc


if ($rpl_debug)
{
  --echo ---- Check input ----
  --echo MASTER_MYPORT='$MASTER_MYPORT' SLAVE_MYPORT='$SLAVE_MYPORT' MASTER_MYPORT1='$MASTER_MYPORT1' SLAVE_MYPORT1='$SLAVE_MYPORT1'
}

# Allow $MASTER_MYPORT as alias for $SERVER_MYPORT_1
if (!$SERVER_MYPORT_1)
{
  --let SERVER_MYPORT_1= $MASTER_MYPORT
}
# Allow $SLAVE_MYPORT as alias for $SERVER_MYPORT_2
if (!$SERVER_MYPORT_2)
{
  --let SERVER_MYPORT_2= $SLAVE_MYPORT
}
# Check that $rpl_server_count is set
if (!$rpl_server_count)
{
  --let $_compute_rpl_server_count= `SELECT REPLACE('$rpl_topology', '->', ',')`
  --let $rpl_server_count= `SELECT GREATEST($_compute_rpl_server_count)`
}


if ($rpl_debug)
{
  --echo ---- Setup connections and reset each server ----
}


if (!$rpl_debug)
{
  --disable_query_log
}


# Create connection to the server; reset master/slave, select
# database, set autoinc variables.
--let $_rpl_server= $rpl_server_count
while ($_rpl_server)
{
  # Connect.
  --let $rpl_server_number= $_rpl_server
  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connect.inc

  # Configure server.
  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connection.inc
  USE test;
  if (!$rpl_skip_reset_master_and_slave)
  {
    RESET MASTER;
    SET GLOBAL gtid_slave_pos= "";
    RESET SLAVE;
  }

  --dec $_rpl_server
}


# Signal that initialization is done and all connections created.
--let $rpl_inited= 1

# Assert that all hosts have different server_ids
if ($rpl_check_server_ids)
{
  if ($rpl_debug)
  {
    --echo ---- Check that @@server_id is distinct for all servers ----
  }

  --let $_rpl_server= $rpl_server_count
  while ($_rpl_server)
  {
    --let $_rpl_server2= $_rpl_server
    --dec $_rpl_server2
    while ($_rpl_server2)
    {
      --let $assert_text= Servers $_rpl_server and $_rpl_server2 should have different @@server_id
      --let $assert_cond= [SELECT @@server_id AS i, i, 1] != $_rpl_server

      --source include/assert.inc
      --dec $_rpl_server2
    }
    --dec $_rpl_server
  }
}

if ($rpl_debug)
{
  --echo ---- Check the topology and call CHANGE MASTER ----
}

# $rpl_master_list must be set so that include/rpl_change_topology.inc and later
# include/rpl_for_each_slave.inc knows which servers are initialized and not.
--let $rpl_master_list= `SELECT REPEAT('x', $rpl_server_count * LENGTH($rpl_server_count))`
--source include/rpl_change_topology.inc


if (!$rpl_skip_start_slave)
{
  if ($rpl_debug)
  {
    --echo ---- Start slaves ----
  }
  --source include/rpl_start_slaves.inc
}

if ($rpl_debug)
{
  --echo ---- Set connection to the server_1 ----
}
--let $rpl_connection_name= server_1
--source include/rpl_connection.inc


--let $skip_restore_connection= 1
--let $include_filename= rpl_init.inc
--source include/end_include_file.inc
