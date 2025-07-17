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

#pragma once

#include <libyul/backends/evm/ControlFlow.h>

#include <libsolutil/JSON.h>

using namespace solidity;
using namespace yul;

class YulControlFlowGraphExporter
{
public:
	YulControlFlowGraphExporter(Dialect const& _dialect, ControlFlow const& _controlFlow)
		: m_dialect(_dialect), m_controlFlow(_controlFlow)
	{
	}

	Json run();

private:
	// AST-level exporting
	Json exportLiteral(Literal const& _literal);
	Json exportExpression(Expression const& _expr);
	Json exportExpressionList(std::vector<Expression> const& _exprs);

	// CFG-level exporting
	Json exportValue(SSACFG const& _cfg, SSACFG::ValueId _valueId);
	Json exportValueList(SSACFG const& _cfg, std::vector<SSACFG::ValueId> const& _values);
	Json exportOperation(SSACFG const& _cfg, SSACFG::Operation const& _operation);
	Json exportBlock(SSACFG const& _cfg, SSACFG::BlockId _blockId);
	Json exportFunction(SSACFG const& _cfg);

	// fields
	Dialect const& m_dialect;
	ControlFlow const& m_controlFlow;
};
