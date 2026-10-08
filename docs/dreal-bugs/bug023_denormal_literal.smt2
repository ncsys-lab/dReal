(set-logic QF_NRA)
(declare-fun x () Real [0, 1])
(assert (<= x 1e-320))
(check-sat)
