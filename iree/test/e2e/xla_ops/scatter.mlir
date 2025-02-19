func @scatter_update_scalar_1D() {
  %arg0 = iree.unfoldable_constant dense<0> : tensor<8xi32>
  %arg1 = iree.unfoldable_constant dense<[[1], [3], [4], [7]]> : tensor<4x1xi32>
  %arg2 = iree.unfoldable_constant dense<[9, 10, 11, 12]> : tensor<4xi32>
  %0 = "mhlo.scatter"(%arg0, %arg1, %arg2) ( {
  ^bb0(%arg3: tensor<i32>, %arg4: tensor<i32>):  // no predecessors
    "mhlo.return"(%arg4) : (tensor<i32>) -> ()
  }) {
    indices_are_sorted = false,
    scatter_dimension_numbers = {
      index_vector_dim = 1 : i64,
      inserted_window_dims = dense<0> : tensor<1xi64>,
      scatter_dims_to_operand_dims = dense<0> : tensor<1xi64>,
      update_window_dims = dense<> : tensor<0xi64>
    },
    unique_indices = false
  } : (tensor<8xi32>, tensor<4x1xi32>, tensor<4xi32>) -> tensor<8xi32>
  check.expect_eq_const(%0, dense<[0, 9, 0, 10, 11, 0, 0, 12]> : tensor<8xi32>) : tensor<8xi32>
  return
}

func @scatter_update_scalar_2D() {
  %arg0 = iree.unfoldable_constant dense<0> : tensor<4x3xi32>
  %arg1 = iree.unfoldable_constant dense<[[0, 0], [1, 1], [2, 2]]> : tensor<3x2xi32>
  %arg2 = iree.unfoldable_constant dense<[1, 2, 3]> : tensor<3xi32>
  %0 = "mhlo.scatter"(%arg0, %arg1, %arg2) ( {
  ^bb0(%arg3: tensor<i32>, %arg4: tensor<i32>):  // no predecessors
    "mhlo.return"(%arg4) : (tensor<i32>) -> ()
  }) {indices_are_sorted = false,
      scatter_dimension_numbers = {
        index_vector_dim = 1 : i64,
        inserted_window_dims = dense<[0, 1]> : tensor<2xi64>,
        scatter_dims_to_operand_dims = dense<[0, 1]> : tensor<2xi64>,
        update_window_dims = dense<> : tensor<0xi64>
      },
      unique_indices = false
  } : (tensor<4x3xi32>, tensor<3x2xi32>, tensor<3xi32>) -> tensor<4x3xi32>
  check.expect_eq_const(%0, dense<[[1, 0, 0],
                                   [0, 2, 0],
                                   [0, 0, 3],
                                   [0, 0, 0]]> : tensor<4x3xi32>) : tensor<4x3xi32>
  return
}

func @scatter_update_slice_2D() {
  %arg0 = iree.unfoldable_constant dense<0> : tensor<6x3xi32>
  %arg1 = iree.unfoldable_constant dense<[[2], [4]]> : tensor<2x1xi32>
  %arg2 = iree.unfoldable_constant dense<[[1, 2, 3],
                                          [4, 5, 6]]> : tensor<2x3xi32>
  %0 = "mhlo.scatter"(%arg0, %arg1, %arg2) ( {
  ^bb0(%arg3: tensor<i32>, %arg4: tensor<i32>):  // no predecessors
    "mhlo.return"(%arg4) : (tensor<i32>) -> ()
  }) {
    indices_are_sorted = false,
    scatter_dimension_numbers = {
      index_vector_dim = 1 : i64,
      inserted_window_dims = dense<0> : tensor<1xi64>,
      scatter_dims_to_operand_dims = dense<0> : tensor<1xi64>,
      update_window_dims = dense<1> : tensor<1xi64>
    },
    unique_indices = false
  } : (tensor<6x3xi32>, tensor<2x1xi32>, tensor<2x3xi32>) -> tensor<6x3xi32>
  check.expect_eq_const(%0, dense<[[0, 0, 0],
                                   [0, 0, 0],
                                   [1, 2, 3],
                                   [0, 0, 0],
                                   [4, 5, 6],
                                   [0, 0, 0]]> : tensor<6x3xi32>) : tensor<6x3xi32>
  return
}

func @scatter_add_slice_2D() {
  %arg0 = iree.unfoldable_constant dense<1> : tensor<6x3xi32>
  %arg1 = iree.unfoldable_constant dense<[[2], [4]]> : tensor<2x1xi32>
  %arg2 = iree.unfoldable_constant dense<[[1, 2, 3],
                                          [4, 5, 6]]> : tensor<2x3xi32>
  %0 = "mhlo.scatter"(%arg0, %arg1, %arg2) ( {
  ^bb0(%arg3: tensor<i32>, %arg4: tensor<i32>):  // no predecessors
    %1 = mhlo.add %arg3, %arg4 : tensor<i32>
    "mhlo.return"(%1) : (tensor<i32>) -> ()
  }) {
    indices_are_sorted = false,
    scatter_dimension_numbers = {
      index_vector_dim = 1 : i64,
      inserted_window_dims = dense<0> : tensor<1xi64>,
      scatter_dims_to_operand_dims = dense<0> : tensor<1xi64>,
      update_window_dims = dense<1> : tensor<1xi64>
    },
    unique_indices = false
  } : (tensor<6x3xi32>, tensor<2x1xi32>, tensor<2x3xi32>) -> tensor<6x3xi32>
  check.expect_eq_const(%0, dense<[[1, 1, 1],
                                   [1, 1, 1],
                                   [2, 3, 4],
                                   [1, 1, 1],
                                   [5, 6, 7],
                                   [1, 1, 1]]> : tensor<6x3xi32>) : tensor<6x3xi32>
  return
}

func @scatter_add_slice_2D_dynamic_num_updates() {
  %arg0 = iree.unfoldable_constant dense<1> : tensor<6x3xi32>
  %arg1 = iree.dynamic_shape_constant dense<[[2], [4]]> : tensor<2x1xi32> -> tensor<?x1xi32>
  %arg2 = iree.dynamic_shape_constant dense<[[1, 2, 3],
                                             [4, 5, 6]]> : tensor<2x3xi32> -> tensor<?x3xi32>
  %0 = "mhlo.scatter"(%arg0, %arg1, %arg2) ( {
  ^bb0(%arg3: tensor<i32>, %arg4: tensor<i32>):  // no predecessors
    %1 = mhlo.add %arg3, %arg4 : tensor<i32>
    "mhlo.return"(%1) : (tensor<i32>) -> ()
  }) {
    indices_are_sorted = false,
    scatter_dimension_numbers = {
      index_vector_dim = 1 : i64,
      inserted_window_dims = dense<0> : tensor<1xi64>,
      scatter_dims_to_operand_dims = dense<0> : tensor<1xi64>,
      update_window_dims = dense<1> : tensor<1xi64>
    },
    unique_indices = false
  } : (tensor<6x3xi32>, tensor<?x1xi32>, tensor<?x3xi32>) -> tensor<6x3xi32>
  check.expect_eq_const(%0, dense<[[1, 1, 1],
                                   [1, 1, 1],
                                   [2, 3, 4],
                                   [1, 1, 1],
                                   [5, 6, 7],
                                   [1, 1, 1]]> : tensor<6x3xi32>) : tensor<6x3xi32>
  return
}

func @scatter_1D_large() {
  %original = iree.unfoldable_constant dense<1> : tensor<1400xi32>
  %update = iree.unfoldable_constant dense<2> : tensor<1400xi32>
  %init = linalg.init_tensor [1400] : tensor<1400xi32>
  %indices = linalg.generic {
      indexing_maps = [affine_map<(d0) -> (d0)>],
      iterator_types = ["parallel"]}
      outs(%init : tensor<1400xi32>) {
      ^bb0(%arg0: i32):
        %0 = linalg.index 0 : index
     %1 = index_cast %0 : index to i32
     linalg.yield %1 : i32
      } -> tensor<1400xi32>
  %indices_reshaped = linalg.tensor_expand_shape %indices [[0, 1]] :
      tensor<1400xi32> into tensor<1400x1xi32>
  %result = "mhlo.scatter"(%original, %indices_reshaped, %update)({
    ^bb0(%arg3 : tensor<i32>, %arg4 : tensor<i32>):
      "mhlo.return"(%arg4) : (tensor<i32>) -> ()
    }) {
    indices_are_sorted = false,
    scatter_dimension_numbers = {
      index_vector_dim = 1: i64,
      inserted_window_dims = dense<0> : tensor<1xi64>,
      scatter_dims_to_operand_dims = dense<0> : tensor<1xi64>,
      update_window_dims = dense<> : tensor<0xi64>
    },
    unique_indices = false
  } : (tensor<1400xi32>, tensor<1400x1xi32>, tensor<1400xi32>) -> tensor<1400xi32>
  check.expect_eq_const(%result, dense<2> : tensor<1400xi32>) : tensor<1400xi32>
  return
}

func @scatter_2D_large() {
  %original = iree.unfoldable_constant dense<1> : tensor<200x300xi32>
  %update = iree.unfoldable_constant dense<2> : tensor<200x300xi32>
  %init = linalg.init_tensor [200] : tensor<200xi32>
  %indices = linalg.generic {
      indexing_maps = [affine_map<(d0) -> (d0)>],
      iterator_types = ["parallel"]}
      outs(%init : tensor<200xi32>) {
      ^bb0(%arg0: i32):
        %0 = linalg.index 0 : index
        %1 = index_cast %0 : index to i32
        linalg.yield %1 : i32
      } -> tensor<200xi32>
  %indices_reshaped = linalg.tensor_expand_shape %indices [[0, 1]] :
      tensor<200xi32> into tensor<200x1xi32>
  %result = "mhlo.scatter"(%original, %indices_reshaped, %update)({
    ^bb0(%arg3 : tensor<i32>, %arg4 : tensor<i32>):
      "mhlo.return"(%arg4) : (tensor<i32>) -> ()
    }) {
    indices_are_sorted = false,
    scatter_dimension_numbers = {
      index_vector_dim = 1: i64,
      inserted_window_dims = dense<0> : tensor<1xi64>,
      scatter_dims_to_operand_dims = dense<0> : tensor<1xi64>,
      update_window_dims = dense<1> : tensor<0xi64>
    },
    unique_indices = false
  } : (tensor<200x300xi32>, tensor<200x1xi32>, tensor<200x300xi32>) -> tensor<200x300xi32>
  check.expect_eq_const(%result, dense<2> : tensor<200x300xi32>) : tensor<200x300xi32>
  return
}
