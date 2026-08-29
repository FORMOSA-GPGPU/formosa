SELECT
  ts,
  dur,
  name
FROM
  slice
WHERE
  category = 'PerfettoReader'
ORDER BY
  ts;
