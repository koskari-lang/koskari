# KOS_OP_COMBINE_ARGS
- **Style**: COUNT. Arg = count of sequences to combine.
- **Stack**: `[..., t_0, ..., t_{m-1}] → [..., tuple]`. Concatenate the m sequences (each via iteration) into a single tuple. Non-tuple values are converted with PySequence_Tuple when m == 1.

