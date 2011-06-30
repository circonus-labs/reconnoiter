struct Metric
{
  1: byte metricType
  2: optional double valueDbl
  3: optional i64 valueI64
  4: optional i32 valueI32
  5: optional string valueStr
}

struct Bundle
{
  1: byte available,
  2: byte state,
  3: i32 duration,
  4: string status,
  5: optional map<string, Metric> metrics,
}
