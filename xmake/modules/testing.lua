-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Native on_test adapter for programs whose interpreter is not targetfile().
-- Keep results in Xmake's test report, including a failed command's output.
function run_with(program, args, opt)
  local outfile, errfile = os.tmpfile(), os.tmpfile()
  local code, errors = os.execv(program, args, {
    try = true,
    curdir = opt.rundir,
    envs = opt.runenvs,
    timeout = opt.run_timeout,
    stdout = outfile,
    stderr = errfile,
  })
  opt.stdout = os.isfile(outfile) and io.readfile(outfile) or ""
  opt.stderr = os.isfile(errfile) and io.readfile(errfile) or ""
  os.tryrm(outfile)
  os.tryrm(errfile)
  local passed = code == 0
  if not passed then opt.errors = errors or ("command exited with " .. tostring(code)) end
  for _, pattern in ipairs(table.wrap(opt.fail_outputs)) do
    if ((opt.stdout or "") .. (opt.stderr or "")):match(pattern) then
      opt.errors = "matched failed output: " .. pattern
      passed = false
    end
  end
  return passed
end
