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
  1: string id,
  2: string checkModule,
  3: string target,
  4: string name,
  5: byte available,
  6: byte state,
  7: i32 duration,
  8: string status,
  9: i64 timestamp,
  10: optional map<string, Metric> metrics,
}
