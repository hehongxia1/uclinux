;;; GNUTLS --- Guile bindings for GnuTLS.
;;; Copyright (C) 2007  Free Software Foundation
;;;
;;; GNUTLS is free software; you can redistribute it and/or
;;; modify it under the terms of the GNU Lesser General Public
;;; License as published by the Free Software Foundation; either
;;; version 2.1 of the License, or (at your option) any later version.
;;;
;;; GNUTLS is distributed in the hope that it will be useful,
;;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;;; Lesser General Public License for more details.
;;;
;;; You should have received a copy of the GNU Lesser General Public
;;; License along with GNUTLS; if not, write to the Free Software
;;; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

;;; Written by Ludovic Court�s <ludo@chbouib.org>.


;;;
;;; Test SRP base64 encoding and decoding.
;;;

(use-modules (gnutls))

(define %message
  "GNUTLS is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.")

(exit (let ((encoded (srp-base64-encode %message)))
        (and (string? encoded)
             (string=? (srp-base64-decode encoded)
                       %message))))


;;; arch-tag: ea1534a5-d513-4208-9a75-54bd4710f915
