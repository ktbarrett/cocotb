-- Copyright cocotb contributors
-- Licensed under the Revised BSD License, see LICENSE for details.
-- SPDX-License-Identifier: BSD-3-Clause USE OF THIS
-- SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

entity test is
end entity test;

architecture behav of test is
    signal a : bit;
    signal b : bit;
begin
	process is
	begin
        a <= '0';
        b <= '0';
        wait for 10 ns;
        a <= '1';
        b <= '1';
        wait for 10 ns;
	end process;
end architecture behav;
