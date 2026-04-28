/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

/**
   CAPTURE_CC: arg unused. Capture the undelimited continuation up to the
   current CC block and push it (KosPyContinuation) on the stack.
*/

KOS_OP_HANDLER(CAPTURE_CC) {
  KosContinuation *k = NULL;
  if (kos_capture_cc(es, &k) < 0) {
    return KOS_OP_POST_ERR();
  }
  PyObject *wrapper = KosPyContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    return KOS_OP_POST_ERR();
  }
  return kos_op_outcome_PUSH(es, wrapper);
}
