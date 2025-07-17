/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <libyul/Utilities.h>
#include <libyul/YulControlFlowGraphExporter.h>

#include <libsolutil/Algorithms.h>
#include <libsolutil/Numeric.h>
#include <libsolutil/Visitor.h>

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

using namespace solidity;
using namespace solidity::langutil;
using namespace solidity::util;
using namespace solidity::yul;

Json YulControlFlowGraphExporter::exportLiteral(Literal const& _literal)
{
	Json result = Json::object();
	switch (_literal.kind)
	{
	case LiteralKind::Boolean:
	{
		result["type"] = "Boolean";
		result["value"] = _literal.value.value() ? "true" : "false";
		break;
	}
	case LiteralKind::Number:
	{
		result["type"] = "Number";
		result["value"] = toCompactHexWithPrefix(_literal.value.value());
		break;
	}
	case LiteralKind::String:
	{
		result["type"] = "String";
		result["value"] = _literal.value.builtinStringLiteralValue();
		break;
	}
	}
	return result;
}

Json YulControlFlowGraphExporter::exportExpression(Expression const& _expr)
{
	Json result = Json::object();
	std::visit(
		GenericVisitor{
			[&](const FunctionCall& call)
			{
				std::visit(
					GenericVisitor{
						[&](const Identifier& ident)
						{
							result["type"] = "FunctionCall";
							result["op"] = ident.name.str();
							result["arguments"] = exportExpressionList(call.arguments);
						},
						[&](const BuiltinName& builtin)
						{
							result["type"] = "BuiltinCall";
							result["op"] = m_dialect.builtin(builtin.handle).name;
							result["arguments"] = exportExpressionList(call.arguments);
						},
					},
					call.functionName);
			},
			[&](const Identifier& ident)
			{
				result["type"] = "Identifier";
				result["name"] = ident.name.str();
			},
			[&](const Literal& literal)
			{
				result["type"] = "Literal";
				result["data"] = exportLiteral(literal);
			},
		},
		_expr);
	return result;
}

Json YulControlFlowGraphExporter::exportExpressionList(std::vector<Expression> const& _exprs)
{
	Json result = Json::array();
	for (auto const& expr: _exprs)
		result.push_back(exportExpression(expr));
	return result;
}

Json YulControlFlowGraphExporter::exportValue(SSACFG const& _cfg, SSACFG::ValueId _valueId)
{
	yulAssert(_valueId.value != std::numeric_limits<size_t>::max());

	Json result = Json::object();
	auto const& info = _cfg.valueInfo(_valueId);
	std::visit(
		GenericVisitor{
			[&](SSACFG::UnreachableValue const&) { result["type"] = "Unreachable"; },
			[&](SSACFG::LiteralValue const& _literal)
			{
				result["type"] = "Literal";
				result["value"] = toCompactHexWithPrefix(_literal.value);
			},
			[&](SSACFG::VariableValue const&)
			{
				result["type"] = "Variable";
				result["id"] = _valueId.value;
			},
			[&](SSACFG::PhiValue const&)
			{
				result["type"] = "Variable"; // yes, intentionally keep it as Variable type
				result["id"] = _valueId.value;
			},
		},
		info);
	return result;
}

Json YulControlFlowGraphExporter::exportValueList(SSACFG const& _cfg, std::vector<SSACFG::ValueId> const& _values)
{
	Json result = Json::array();
	for (auto const& value: _values)
		result.push_back(exportValue(_cfg, value));
	return result;
}

Json YulControlFlowGraphExporter::exportOperation(SSACFG const& _cfg, SSACFG::Operation const& _operation)
{
	Json result = Json::object();
	std::visit(
		GenericVisitor{
			[&](SSACFG::Call const& _call)
			{
				auto const& calleeName = _call.call.get().functionName;
				auto const& calleeIdent = _call.function.get().name;
				yulAssert(std::holds_alternative<Identifier>(calleeName));
				yulAssert(std::get<Identifier>(calleeName).name == calleeIdent);

				result["type"] = "FunctionCall";
				result["op"] = calleeIdent.str();
				result["arguments"] = exportExpressionList(_call.call.get().arguments);
			},
			[&](SSACFG::BuiltinCall const& _call)
			{
				auto const& calleeName = _call.call.get().functionName;
				auto const& calleeIdent = _call.builtin.get().name;
				yulAssert(std::holds_alternative<BuiltinName>(calleeName));
				yulAssert(m_dialect.builtin(std::get<BuiltinName>(calleeName).handle).name == calleeIdent);

				result["type"] = "BuiltinCall";
				result["op"] = calleeIdent;
				result["arguments"] = exportExpressionList(_call.call.get().arguments);
			},
			[&](SSACFG::LiteralAssignment const&)
			{
				yulAssert(_operation.inputs.size() == 1);
				yulAssert(_cfg.isLiteralValue(_operation.inputs.back()));
				result["type"] = "LiteralAssignment";
			},
		},
		_operation.kind);

	result["in"] = exportValueList(_cfg, _operation.inputs);
	result["out"] = exportValueList(_cfg, _operation.outputs);
	return result;
}

Json YulControlFlowGraphExporter::exportBlock(SSACFG const& _cfg, SSACFG::BlockId _blockId)
{
	Json result = Json::object();
	auto const& block = _cfg.block(_blockId);

	result["label"] = _blockId.value;
	result["entries"] = block.entries | ranges::views::transform([](auto const& entry) { return entry.value; })
						| ranges::to<Json::array_t>();

	Json phi_nodes = Json::array();
	for (auto const& phi: block.phis)
	{
		auto* phiInfo = std::get_if<SSACFG::PhiValue>(&_cfg.valueInfo(phi));
		yulAssert(phiInfo);

		Json phiJson = Json::object();
		phiJson["in"] = exportValueList(_cfg, phiInfo->arguments);
		phiJson["out"] = exportValue(_cfg, phi);
		phi_nodes.push_back(phiJson);
	}
	result["phi_nodes"] = phi_nodes;

	Json instructions = Json::array();
	for (auto const& operation: block.operations)
		instructions.push_back(exportOperation(_cfg, operation));
	result["instructions"] = instructions;

	Json exit = Json::object();
	std::visit(
		util::GenericVisitor{
			[&](SSACFG::BasicBlock::MainExit const&) { exit["type"] = "MainExit"; },
			[&](SSACFG::BasicBlock::Jump const& _jump)
			{
				exit["type"] = "Jump";
				exit["target"] = _jump.target.value;
			},
			[&](SSACFG::BasicBlock::ConditionalJump const& _conditionalJump)
			{
				exit["type"] = "ConditionalJump";
				exit["cond"] = exportValue(_cfg, _conditionalJump.condition);
				exit["target0"] = _conditionalJump.zero.value;
				exit["target1"] = _conditionalJump.nonZero.value;
			},
			[&](SSACFG::BasicBlock::FunctionReturn const& _return)
			{
				exit["type"] = "FunctionReturn";
				exit["return_values"] = exportValueList(_cfg, _return.returnValues);
			},
			[&](SSACFG::BasicBlock::Terminated const&) { exit["type"] = "Terminated"; },
			[&](SSACFG::BasicBlock::JumpTable const&) { yulAssert(false); }},
		block.exit);
	result["exit"] = exit;

	return result;
}

Json YulControlFlowGraphExporter::exportFunction(SSACFG const& _cfg)
{
	Json result = Json::object();

	Json params = Json::array();
	for (auto const& [argVar, argId]: _cfg.arguments)
	{
		Json paramJson = Json::object();
		paramJson["name"] = argVar.get().name.str();
		paramJson["repr"] = exportValue(_cfg, argId);
		params.push_back(paramJson);
	}
	result["params"] = params;

	Json rets = Json::array();
	for (auto const& retVar: _cfg.returns)
	{
		rets.push_back(retVar.get().name.str());
	}
	result["returns"] = rets;

	Json blocks = Json::array();
	util::BreadthFirstSearch<SSACFG::BlockId> bfs{{{_cfg.entry}}};
	bfs.run(
		[&](auto _blockId, auto _addChild)
		{
			blocks.push_back(exportBlock(_cfg, _blockId));

			// add children for bfs
			auto const& block = _cfg.block(_blockId);
			std::visit(
				util::GenericVisitor{
					[&](SSACFG::BasicBlock::MainExit const&) {},
					[&](SSACFG::BasicBlock::Jump const& _jump) { _addChild(_jump.target); },
					[&](SSACFG::BasicBlock::ConditionalJump const& _conditionalJump)
					{
						_addChild(_conditionalJump.zero);
						_addChild(_conditionalJump.nonZero);
					},
					[&](SSACFG::BasicBlock::FunctionReturn const&) {},
					[&](SSACFG::BasicBlock::Terminated const&) {},
					[&](SSACFG::BasicBlock::JumpTable const&) { yulAssert(false); }},
				block.exit);
		});
	result["blocks"] = blocks;

	result["entry"] = _cfg.entry.value;
	result["exits"] = _cfg.exits | ranges::views::transform([](auto const& entry) { return entry.value; })
					  | ranges::to<Json::array_t>();

	return result;
}

Json YulControlFlowGraphExporter::run()
{
	Json result = Json::object();
	result["main"] = exportFunction(*m_controlFlow.mainGraph);

	Json subs = Json::object();
	for (auto const& [function, functionGraph]: m_controlFlow.functionGraphMapping)
	{
		auto name = function->name.str();
		yulAssert(!subs.contains(name));
		subs[name] = exportFunction(*functionGraph);
	}
	result["functions"] = subs;

	return result;
}
