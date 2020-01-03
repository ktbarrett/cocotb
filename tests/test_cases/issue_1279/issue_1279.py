"""
Test that once a SimFailure occurs, no further tests are run
"""
import cocotb


@cocotb.test(expect_error=cocotb.result.SimFailure, stage=1)
def test_sim_failure_a(dut):
    # invoke a deadlock
    yield cocotb.triggers.RisingEdge(dut.clk)


@cocotb.test(stage=2)
def test_sim_failure_b(dut):
    yield cocotb.triggers.NullTrigger()
    raise cocotb.result.TestFailure("This test should never run")
