use signet_forge::{
    ParquetReader, ParquetWriter, SchemaBuilder, WriterOptions,
};
use tempfile::NamedTempFile;

#[test]
fn roundtrip_all_types() {
    let tmp = NamedTempFile::new().unwrap();
    let path = tmp.path().to_str().unwrap().to_string();

    // Build schema with all 6 types
    let schema = SchemaBuilder::new("test_all_types").unwrap()
        .add_bool("flag")
        .add_int32("count")
        .add_int64("timestamp")
        .add_float("ratio")
        .add_double("price")
        .add_string("symbol")
        .build()
        .unwrap();

    assert_eq!(schema.num_columns(), 6);
    assert_eq!(schema.column_name(0), Some("flag".to_string()));
    assert_eq!(schema.column_name(5), Some("symbol".to_string()));

    // Write
    let mut writer = ParquetWriter::open(&path, &schema, None).unwrap();
    assert!(writer.is_open());

    writer
        .write_column_bool(0, &[true, false, true])
        .unwrap();
    writer
        .write_column_int32(1, &[10, 20, 30])
        .unwrap();
    writer
        .write_column_int64(2, &[1000, 2000, 3000])
        .unwrap();
    writer
        .write_column_float(3, &[1.5_f32, 2.5, 3.5])
        .unwrap();
    writer
        .write_column_double(4, &[100.1, 200.2, 300.3])
        .unwrap();
    writer
        .write_column_string(5, &["AAPL", "GOOG", "MSFT"])
        .unwrap();

    writer.flush_row_group().unwrap();
    assert_eq!(writer.rows_written(), 3);
    writer.close().unwrap();

    // Read
    let mut reader = ParquetReader::open(&path).unwrap();

    assert_eq!(reader.num_rows(), 3);
    assert_eq!(reader.num_row_groups(), 1);

    let created_by = reader.created_by();
    assert!(created_by.contains("signet"), "created_by: {}", created_by);

    let schema_r = reader.schema();
    assert_eq!(schema_r.num_columns(), 6);

    // Bool
    let bools = reader.read_column_bool(0, 0).unwrap();
    assert_eq!(bools, vec![true, false, true]);

    // Int32
    let ints = reader.read_column_int32(0, 1).unwrap();
    assert_eq!(ints, vec![10, 20, 30]);

    // Int64
    let longs = reader.read_column_int64(0, 2).unwrap();
    assert_eq!(longs, vec![1000, 2000, 3000]);

    // Float
    let floats = reader.read_column_float(0, 3).unwrap();
    assert_eq!(floats, vec![1.5_f32, 2.5, 3.5]);

    // Double
    let doubles = reader.read_column_double(0, 4).unwrap();
    assert_eq!(doubles, vec![100.1, 200.2, 300.3]);

    // String
    let strings = reader.read_column_string(0, 5).unwrap();
    assert_eq!(
        strings,
        vec!["AAPL".to_string(), "GOOG".to_string(), "MSFT".to_string()]
    );
}

#[test]
fn roundtrip_with_options() {
    let tmp = NamedTempFile::new().unwrap();
    let path = tmp.path().to_str().unwrap().to_string();

    let schema = SchemaBuilder::new("opts_test").unwrap()
        .add_double("value")
        .build()
        .unwrap();

    let opts = WriterOptions::new().row_group_size(2);

    let mut writer = ParquetWriter::open(&path, &schema, Some(opts)).unwrap();
    writer.write_column_double(0, &[1.0, 2.0]).unwrap();
    writer.flush_row_group().unwrap();
    writer.write_column_double(0, &[3.0]).unwrap();
    writer.flush_row_group().unwrap();
    writer.close().unwrap();

    let mut reader = ParquetReader::open(&path).unwrap();
    assert_eq!(reader.num_rows(), 3);
    assert_eq!(reader.num_row_groups(), 2);

    let rg0 = reader.read_column_double(0, 0).unwrap();
    assert_eq!(rg0, vec![1.0, 2.0]);

    let rg1 = reader.read_column_double(1, 0).unwrap();
    assert_eq!(rg1, vec![3.0]);
}

#[test]
fn version_string() {
    let v = signet_forge::version();
    assert_eq!(v, "0.1.0");
}

#[test]
fn error_on_bad_path() {
    let result = ParquetReader::open("/nonexistent/path/to/file.parquet");
    assert!(result.is_err());
    match result {
        Err(err) => assert_eq!(err.code, signet_forge::ErrorCode::IoError),
        Ok(_) => panic!("expected error"),
    }
}

#[test]
fn schema_builder_column_types() {
    let schema = SchemaBuilder::new("type_check").unwrap()
        .add_bool("b")
        .add_int32("i32")
        .add_int64("i64")
        .add_float("f32")
        .add_double("f64")
        .add_string("str")
        .build()
        .unwrap();

    use signet_forge::PhysicalType;
    assert_eq!(schema.column_physical_type(0), Some(PhysicalType::Boolean));
    assert_eq!(schema.column_physical_type(1), Some(PhysicalType::Int32));
    assert_eq!(schema.column_physical_type(2), Some(PhysicalType::Int64));
    assert_eq!(schema.column_physical_type(3), Some(PhysicalType::Float));
    assert_eq!(schema.column_physical_type(4), Some(PhysicalType::Double));
    assert_eq!(schema.column_physical_type(5), Some(PhysicalType::ByteArray));
}
