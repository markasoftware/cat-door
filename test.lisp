(asdf:load-system :gooptest)

(defpackage :cat-door-tests
  (:use :cl :gooptest :gooptest-avr))

(in-package :cat-door-tests)

(defun flashing-p ()
  (< 0.45 (pin-duty-cycle :b2 :stop '(1 :s) :skip '(1 :ms) :pull :up) 0.55))

;; a4 = open, a5 = closed, active high
(defun set-switch-open ()
  (setf (pin :a4) :high)
  (setf (pin :a5) :low))
(defun set-switch-one-way ()
  (setf (pin :a4) :low)
  (setf (pin :a5) :low))
(defun set-switch-closed ()
  (setf (pin :a4) :low)
  (setf (pin :a5) :high))

;; a6 = open, a7 = closed, active low
(defun set-sensor-open ()
  (setf (pin :a6) :low)
  (setf (pin :a7) :high))
(defun set-sensor-floating ()
  (setf (pin :a6) :high)
  (setf (pin :a7) :high))
(defun set-sensor-closed ()
  (setf (pin :a6) :high)
  (setf (pin :a7) :low))

(defun motor (pin1 pin2)
  (ecase (pin pin1)
    (:high (ecase (pin pin2)
             (:low :opening)))
    (:low (ecase (pin pin2)
            (:high :closing)
            (:low :idling)))))

;; a0 = inner up, a1 = inner down
(defun inner-motor ()
  (motor :a0 :a1))

;; a2 = outer up, a3 = outer down
(defun outer-motor ()
  (motor :a2 :a3))

(defun assert-outer-motor (state)
  (assert (eq state (outer-motor))))
(defun assert-inner-motor (state)
  (assert (eq state (inner-motor))))

(defsuite cat-door
  :core (make-instance 'avr-core
                       :frequency 128000
                       :mcu :attiny84
                       ;; TODO: What path are relative pathnames relative to?
                       :firmware-path #P"/home/markasoftware/Development/cat-door/main.elf"))

(in-suite cat-door)

(runtest "Open outer motor (sw open)"
  (set-switch-open)
  (set-sensor-closed)
  (cycles 3 :s)
  ;(assert (flashing-p))
  (assert-outer-motor :opening))

(runtest "Open outer motor (sw one-way)"
  (set-switch-one-way)
  (set-sensor-closed)
  (cycles 3 :s)
  (assert (flashing-p))
  (assert-outer-motor :opening))

(runtest "Close outer motor (sw closed)"
  (set-switch-closed)
  (set-sensor-open)
  (cycles 3 :s)
  (assert (flashing-p))
  (assert-outer-motor :closing))

(runtest "Toggle outer motor as things change"
  (set-switch-closed)
  (set-sensor-open)
  (cycles 3 :s)
  (assert-outer-motor :closing)

  (set-sensor-closed)
  (cycles 1000)
  (assert-outer-motor :idling)
  (assert (not (flashing-p)))

  (set-sensor-open)
  (cycles 1000)
  (assert-outer-motor :idling)
  (assert (not (flashing-p)))

  (set-sensor-closed)
  (set-switch-one-way)
  ;; inner door triggered when switch open -> one way
  (cycles 3 :s)
  (assert-outer-motor :opening)

  (set-sensor-floating)
  (cycles 1000)
  (assert-outer-motor :opening)

  ;; one way -> closed does not trigger inner
  (set-switch-closed)
  (cycles 1000)
  (assert-outer-motor :closing)

  (set-sensor-closed)
  (cycles 1000)
  (assert-outer-motor :idling))

(runtest "Open inner motor on startup (sw open)"
  (set-switch-open)
  (set-sensor-open)
  (cycles 1000)
  (assert-inner-motor :opening)
  (cycles 3 :s)
  (assert-inner-motor :idling))

(runtest "Close inner motor on startup (sw open)"
  (set-switch-one-way)
  (set-sensor-open)
  (cycles 1000)
  (assert-inner-motor :closing)
  (cycles 3 :s)
  (assert-inner-motor :idling))

(runtest "Close inner motor on startup (sw closed)"
  (set-switch-closed)
  (set-sensor-closed)
  (cycles 1000)
  (assert-inner-motor :closing)
  (cycles 3 :s)
  (assert-inner-motor :idling))

(runtest "Toggles inner motor when things change"
  (set-switch-open)
  (set-sensor-open)
  (cycles 1000)
  (assert-inner-motor :opening)
  (set-switch-closed)
  (cycles 1000)
  ;; Shouldn't get interrupted
  (assert-inner-motor :opening)
  (cycles 3 :s)
  ;; but, once done, it should reverse
  (assert-inner-motor :closing)
  (cycles 3 :s)
  ;; and, eventually, it should stop
  (assert-inner-motor :idling)
  ;; Make sure it resets the timer to zero every time the direction changes
  (dotimes (i 10)
    (set-switch-open)
    (cycles 500 :ms)
    (set-switch-closed)
    (cycles 500 :ms))
  (set-switch-open)
  (cycles 3 :s)
  (assert-inner-motor :opening)
  (cycles 3 :s)
  (assert-inner-motor :idling))

(runtest "Does both inner and outer door operations on startup"
  (set-switch-open)
  (set-sensor-closed)
  (cycles 1000)
  (assert-inner-motor :opening)
  (assert-outer-motor :idling)
  (cycles 3 :s)
  (assert-inner-motor :idling)
  (assert-outer-motor :opening)
  (set-sensor-open)
  (cycles 1000)
  (assert-inner-motor :idling)
  (assert-outer-motor :idling))

(runtest "Re-opens the inner door every so often"
  (set-switch-open)
  (set-sensor-open)
  (cycles 5 :s)
  (assert-inner-motor :idling)

  (assert
   (cycles-between (:start '(1 :m) :stop '(5 :m) :skip '(100 :ms))
     (eq (inner-motor) :opening)))
  (cycles 3 :s)
  (assert-inner-motor :idling))

(runtest "Rewinds once"
  (set-switch-closed)
  (set-sensor-open)
  (cycles 3 :s)
  (assert-outer-motor :closing)
  (set-sensor-floating)
  (cycles 1000)
  (set-sensor-open)
  (cycles 1000)
  (assert-outer-motor :opening)
  (set-sensor-closed)
  (cycles 1000)
  (assert-outer-motor :idling))

;; TODO: do we actually /want/ to rewind multiple times?
(runtest "Rewinds a couple times"
  (set-switch-closed)
  (set-sensor-open)
  (cycles 3 :s)
  (assert-outer-motor :closing)
  (set-sensor-floating)
  (cycles 1000)
  (set-sensor-open)
  (assert-outer-motor :opening)
  (cycles 1000)
  (set-sensor-floating)
  (assert-outer-motor :opening)
  (cycles 1000)
  (set-sensor-open)
  (assert-outer-motor :closing)
  (cycles 1000)
  (set-sensor-closed)
  (cycles 1000)
  (assert-outer-motor :idling))

(runtest "Waits a minute to retry after a botched rewind"
  (set-switch-closed)
  (set-sensor-open)
  (cycles 3 :s)
  (assert-outer-motor :closing))

;; More:
;; + Switch changes interrupting an inner door motion
;; + Periodic timer interrupting outer door motion: Interrupt
;; + Periodic timer interrupting rewind: Wait
;; + Switch changes interrupting rewind: Wait for rewind to finish
;; + Switch changes interrupting pause between rewinds: Interrupt
;; + major TODO: Rewind when opening outer door takes too long
;;
;; Have cli- and sei-like functions that only operate on some interrupts so, eg,
;; we can still get sensor interrupts while rewinding while having, eg, switch
;; interrupts disabled? Or we could just use an atomic busy loop for rewind.
