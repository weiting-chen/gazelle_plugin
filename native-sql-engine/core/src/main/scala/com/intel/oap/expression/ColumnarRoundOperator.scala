/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.intel.oap.expression

import com.google.common.collect.Lists

import org.apache.arrow.gandiva.evaluator._
import org.apache.arrow.gandiva.exceptions.GandivaException
import org.apache.arrow.gandiva.expression._
import org.apache.arrow.vector.types.pojo.ArrowType
import org.apache.arrow.vector.types.FloatingPointPrecision
import org.apache.arrow.vector.types.pojo.Field
import org.apache.arrow.vector.types.DateUnit

import org.apache.spark.internal.Logging
import org.apache.spark.sql.catalyst.expressions._
import org.apache.spark.sql.catalyst.optimizer._
import org.apache.spark.sql.types._

import scala.collection.mutable.ListBuffer

class ColumnarRound(child: Expression, scale: Expression, original: Expression)
  extends Round(child: Expression, scale: Expression)
    with ColumnarExpression
    with Logging {
  val gName = "round"

  override def supportColumnarCodegen(args: java.lang.Object): Boolean = {
    codegenFuncList.contains(gName) && 
    child.asInstanceOf[ColumnarExpression].supportColumnarCodegen(args) &&
    scale.asInstanceOf[ColumnarExpression].supportColumnarCodegen(args)
  }

  buildCheck()

  def buildCheck(): Unit = {
    val supportedTypes = List(FloatType, DoubleType, IntegerType, LongType)
    if (supportedTypes.indexOf(child.dataType) == -1 &&
        !child.dataType.isInstanceOf[DecimalType]) {
      throw new UnsupportedOperationException(
        s"${child.dataType} is not supported in ColumnarRound")
    }
  }

  override def doColumnarCodeGen(args: java.lang.Object): (TreeNode, ArrowType) = {
    val (child_node, childType): (TreeNode, ArrowType) =
      child.asInstanceOf[ColumnarExpression].doColumnarCodeGen(args)
    val (scale_node, scaleType): (TreeNode, ArrowType) =
      scale.asInstanceOf[ColumnarExpression].doColumnarCodeGen(args)

    val resultType = CodeGeneration.getResultType(dataType)
    val funcNode = TreeBuilder.makeFunction("round",
      Lists.newArrayList(child_node, scale_node), resultType)
    (funcNode, resultType)
  }
}

object ColumnarRoundOperator {

  def create(child: Expression, scale: Expression, original: Expression): Expression = original match {
    case r: Round =>
      new ColumnarRound(child, scale, original)
    case other =>
      throw new UnsupportedOperationException(s"not currently supported: $other.")
  }
}
