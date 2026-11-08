/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Pins assertion preprocessing and operand evaluation in every build type.
 */

#include <cassert>

#ifdef NDEBUG
#error Test translation units must not define NDEBUG
#endif

int main ()
{
	int evaluations = 0;
	assert (++evaluations == 1);
	// This independent check still fails if assert becomes a no-op.
	return evaluations == 1 ? 0 : 1;
}
