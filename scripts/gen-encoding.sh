#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

SIMTIX_SM=simtix/src/cores

XFORMOSA=$SIMTIX_SM/xformosa.txt
RISCV_OPCODES=third-party/riscv-opcodes
TARGET=$RISCV_OPCODES/extensions/unratified/rv_xformosa

ENCODING_OUT_H=$RISCV_OPCODES/encoding.out.h
ENCODING_H=$SIMTIX_SM/encoding.h

cp $XFORMOSA $TARGET
make -C $RISCV_OPCODES
cp $ENCODING_OUT_H $ENCODING_H
rm $TARGET
