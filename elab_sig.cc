/*
 * Copyright (c) 2000-2012 Stephen Williams (steve@icarus.com)
 * Copyright CERN 2012 / Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

# include "config.h"

# include  <typeinfo>
# include  <cstdlib>
# include  <iostream>

# include  "Module.h"
# include  "PExpr.h"
# include  "PGate.h"
# include  "PGenerate.h"
# include  "PTask.h"
# include  "PWire.h"
# include  "Statement.h"
# include  "compiler.h"
# include  "netlist.h"
# include  "netmisc.h"
# include  "netenum.h"
# include  "netstruct.h"
# include  "netdarray.h"
# include  "netparray.h"
# include  "util.h"
# include  "ivl_assert.h"

using namespace std;

static bool get_const_argument(NetExpr*exp, verinum&res)
{
      switch (exp->expr_type()) {
	  case IVL_VT_REAL: {
	    NetECReal*cv = dynamic_cast<NetECReal*>(exp);
	    if (cv == 0) return false;
	    verireal tmp = cv->value();
	    res = verinum(tmp.as_long());
	    break;
	  }

	  case IVL_VT_BOOL:
	  case IVL_VT_LOGIC: {
	    NetEConst*cv = dynamic_cast<NetEConst*>(exp);
	    if (cv == 0) return false;
	    res = cv->value();
	    break;
	  }

	  default:
	    assert(0);;
      }

      return true;
}

static bool get_const_argument(NetExpr*exp, long&res)
{
      verinum tmp;
      bool rc = get_const_argument(exp, tmp);
      if (rc == false) return false;
      res = tmp.as_long();
      return true;
}

void Statement::elaborate_sig(Design*, NetScope*) const
{
}

bool PScope::elaborate_sig_wires_(Design*des, NetScope*scope) const
{
      bool flag = true;

      for (map<perm_string,PWire*>::const_iterator wt = wires.begin()
		 ; wt != wires.end() ; ++ wt ) {

	    PWire*cur = (*wt).second;
	    NetNet*sig = cur->elaborate_sig(des, scope);

	    if (sig && (sig->scope() == scope)
		&& (sig->port_type() == NetNet::PREF)) {

		  cerr << cur->get_fileline() << ": sorry: "
		       << "Reference ports not supported yet." << endl;
		  des->errors += 1;
	    }


	      /* If the signal is an input and is also declared as a
		 reg, then report an error. */

	    if (sig && (sig->scope() == scope)
		&& (scope->type() == NetScope::MODULE)
		&& (sig->port_type() == NetNet::PINPUT)
		&& (sig->type() == NetNet::REG)) {

		  cerr << cur->get_fileline() << ": error: Port "
		       << cur->basename() << " of module "
		       << scope->module_name()
		       << " is declared as input and as a reg type." << endl;
		  des->errors += 1;
	    }

	    if (sig && (sig->scope() == scope)
		&& (scope->type() == NetScope::MODULE)
		&& (sig->port_type() == NetNet::PINOUT)
		&& (sig->type() == NetNet::REG)) {

		  cerr << cur->get_fileline() << ": error: Port "
		       << cur->basename() << " of module "
		       << scope->module_name()
		       << " is declared as inout and as a reg type." << endl;
		  des->errors += 1;
	    }

	    if (sig && (sig->scope() == scope)
		&& (scope->type() == NetScope::MODULE)
		&& (sig->port_type() == NetNet::PINOUT)
		&& (sig->data_type() == IVL_VT_REAL)) {

		  cerr << cur->get_fileline() << ": error: Port "
		       << cur->basename() << " of module "
		       << scope->module_name()
		       << " is declared as a real inout port." << endl;
		  des->errors += 1;
	    }

      }

      return flag;
}

static void elaborate_sig_funcs(Design*des, NetScope*scope,
				const map<perm_string,PFunction*>&funcs)
{
      typedef map<perm_string,PFunction*>::const_iterator mfunc_it_t;

      for (mfunc_it_t cur = funcs.begin()
		 ; cur != funcs.end() ; ++ cur ) {

	    hname_t use_name ( (*cur).first );
	    NetScope*fscope = scope->child(use_name);
	    if (fscope == 0) {
		  cerr << (*cur).second->get_fileline() << ": internal error: "
		       << "Child scope for function " << (*cur).first
		       << " missing in " << scope_path(scope) << "." << endl;
		  des->errors += 1;
		  continue;
	    }

	    (*cur).second->elaborate_sig(des, fscope);
      }
}

static void elaborate_sig_tasks(Design*des, NetScope*scope,
				const map<perm_string,PTask*>&tasks)
{
      typedef map<perm_string,PTask*>::const_iterator mtask_it_t;

      for (mtask_it_t cur = tasks.begin()
		 ; cur != tasks.end() ; ++ cur ) {
	    NetScope*tscope = scope->child( hname_t((*cur).first) );
	    assert(tscope);
	    (*cur).second->elaborate_sig(des, tscope);
      }
}


bool Module::elaborate_sig(Design*des, NetScope*scope) const
{
      bool flag = true;

	// Scan all the ports of the module, and make sure that each
	// is connected to wires that have port declarations.
      for (unsigned idx = 0 ;  idx < ports.size() ;  idx += 1) {
	    Module::port_t*pp = ports[idx];
	    if (pp == 0)
		  continue;

	      // The port has a name and an array of expressions. The
	      // expression are all identifiers that should reference
	      // wires within the scope.
	    map<perm_string,PWire*>::const_iterator wt;
	    for (unsigned cc = 0 ;  cc < pp->expr.size() ;  cc += 1) {
		  pform_name_t port_path (pp->expr[cc]->path());
		    // A concatenated wire of a port really should not
		    // have any hierarchy.
		  if (port_path.size() != 1) {
			cerr << get_fileline() << ": internal error: "
			     << "Port " << port_path << " has a funny name?"
			     << endl;
			des->errors += 1;
		  }

		  wt = wires.find(peek_tail_name(port_path));

		  if (wt == wires.end()) {
			cerr << get_fileline() << ": error: "
			     << "Port " << port_path << " ("
			     << (idx+1) << ") of module " << mod_name()
			     << " is not declared within module." << endl;
			des->errors += 1;
			continue;
		  }

		  if ((*wt).second->get_port_type() == NetNet::NOT_A_PORT) {
			cerr << get_fileline() << ": error: "
			     << "Port " << pp->expr[cc]->path() << " ("
			     << (idx+1) << ") of module " << mod_name()
			     << " has no direction declaration."
			     << endl;
			des->errors += 1;
		  }
	    }
      }

      flag = elaborate_sig_wires_(des, scope) && flag;

	// Run through all the generate schemes to elaborate the
	// signals that they hold. Note that the generate schemes hold
	// the scopes that they instantiated, so we don't pass any
	// scope in.
      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generate_schemes.begin()
		 ; cur != generate_schemes.end() ; ++ cur ) {
	    (*cur) -> elaborate_sig(des, scope);
      }

	// Get all the gates of the module and elaborate them by
	// connecting them to the signals. The gate may be simple or
	// complex. What we are looking for is gates that are modules
	// that can create scopes and signals.

      const list<PGate*>&gl = get_gates();

      for (list<PGate*>::const_iterator gt = gl.begin()
		 ; gt != gl.end() ; ++ gt ) {

	    flag &= (*gt)->elaborate_sig(des, scope);
      }

	// After all the wires are elaborated, we are free to
	// elaborate the ports of the tasks defined within this
	// module. Run through them now.

      elaborate_sig_funcs(des, scope, funcs);
      elaborate_sig_tasks(des, scope, tasks);

	// initial and always blocks may contain begin-end and
	// fork-join blocks that can introduce scopes. Therefore, I
	// get to scan processes here.

      typedef list<PProcess*>::const_iterator proc_it_t;

      for (proc_it_t cur = behaviors.begin()
		 ; cur != behaviors.end() ; ++ cur ) {

	    (*cur) -> statement() -> elaborate_sig(des, scope);
      }

      return flag;
}

bool PGate::elaborate_sig(Design*, NetScope*) const
{
      return true;
}

bool PGBuiltin::elaborate_sig(Design*, NetScope*) const
{
      return true;
}

bool PGAssign::elaborate_sig(Design*, NetScope*) const
{
      return true;
}

bool PGModule::elaborate_sig_mod_(Design*des, NetScope*scope,
				  Module*rmod) const
{
      bool flag = true;

      NetScope::scope_vec_t instance = scope->instance_arrays[get_name()];

      for (unsigned idx = 0 ;  idx < instance.size() ;  idx += 1) {
	      // I know a priori that the elaborate_scope created the scope
	      // already, so just look it up as a child of the current scope.
	    NetScope*my_scope = instance[idx];
	    assert(my_scope);

	    if (my_scope->parent() != scope) {
		  cerr << get_fileline() << ": internal error: "
		       << "Instance " << scope_path(my_scope)
		       << " is in parent " << scope_path(my_scope->parent())
		       << " instead of " << scope_path(scope)
		       << endl;
	    }
	    assert(my_scope->parent() == scope);

	    if (! rmod->elaborate_sig(des, my_scope))
		  flag = false;

      }

      return flag;
}

	// Not currently used.
#if 0
bool PGModule::elaborate_sig_udp_(Design*des, NetScope*scope, PUdp*udp) const
{
      return true;
}
#endif

bool PGenerate::elaborate_sig(Design*des,  NetScope*container) const
{
      if (direct_nested_)
	    return elaborate_sig_direct_(des, container);

      bool flag = true;

	// Handle the special case that this is a CASE scheme. In this
	// case the PGenerate itself does not have the generated
	// item. Look instead for the case ITEM that has a scope
	// generated for it.
      if (scheme_type == PGenerate::GS_CASE) {
	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: generate case"
		       << " elaborate_sig in scope "
		       << scope_path(container) << "." << endl;

	    typedef list<PGenerate*>::const_iterator generate_it_t;
	    for (generate_it_t cur = generate_schemes.begin()
		       ; cur != generate_schemes.end() ; ++ cur ) {
		  PGenerate*item = *cur;
		  if (item->direct_nested_ || !item->scope_list_.empty()) {
			flag &= item->elaborate_sig(des, container);
		  }
	    }
	    return flag;
      }

      typedef list<NetScope*>::const_iterator scope_list_it_t;
      for (scope_list_it_t cur = scope_list_.begin()
		 ; cur != scope_list_.end() ; ++ cur ) {

	    NetScope*scope = *cur;

	    if (scope->parent() != container)
		  continue;

	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: Elaborate nets in "
		       << "scope " << scope_path(*cur)
		       << " in generate " << id_number << endl;
	    flag = elaborate_sig_(des, *cur) & flag;
      }

      return flag;
}

bool PGenerate::elaborate_sig_direct_(Design*des, NetScope*container) const
{
      if (debug_elaborate)
	    cerr << get_fileline() << ": debug: "
		 << "Direct nesting " << scope_name
		 << " (scheme_type=" << scheme_type << ")"
		 << " elaborate_sig in scope "
		 << scope_path(container) << "." << endl;

	// Elaborate_sig for a direct nested generated scheme knows
	// that there are only sub_schemes to be elaborated.  There
	// should be exactly 1 active generate scheme, search for it
	// using this loop.
      bool flag = true;
      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generate_schemes.begin()
		 ; cur != generate_schemes.end() ; ++ cur ) {
	    PGenerate*item = *cur;
	    if (item->scheme_type == PGenerate::GS_CASE) {
		  for (generate_it_t icur = item->generate_schemes.begin()
			     ; icur != item->generate_schemes.end() ; ++ icur ) {
			PGenerate*case_item = *icur;
			if (case_item->direct_nested_ || !case_item->scope_list_.empty()) {
			      flag &= case_item->elaborate_sig(des, container);
			}
		  }
	    } else {
		  if (item->direct_nested_ || !item->scope_list_.empty()) {
			  // Found the item, and it is direct nested.
			flag &= item->elaborate_sig(des, container);
		  }
	    }
      }
      return flag;
}

bool PGenerate::elaborate_sig_(Design*des, NetScope*scope) const
{
	// Scan the declared PWires to elaborate the obvious signals
	// in the current scope.
      typedef map<perm_string,PWire*>::const_iterator wires_it_t;
      for (wires_it_t wt = wires.begin()
		 ; wt != wires.end() ; ++ wt ) {

	    PWire*cur = (*wt).second;

	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: Elaborate PWire "
		       << cur->basename() << " in scope " << scope_path(scope) << endl;

	    cur->elaborate_sig(des, scope);
      }

      elaborate_sig_funcs(des, scope, funcs);
      elaborate_sig_tasks(des, scope, tasks);

      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generate_schemes.begin()
		 ; cur != generate_schemes.end() ; ++ cur ) {
	    (*cur) -> elaborate_sig(des, scope);
      }

      typedef list<PGate*>::const_iterator pgate_list_it_t;
      for (pgate_list_it_t cur = gates.begin()
		 ; cur != gates.end() ; ++ cur ) {
	    (*cur) ->elaborate_sig(des, scope);
      }

      typedef list<PProcess*>::const_iterator proc_it_t;
      for (proc_it_t cur = behaviors.begin()
		 ; cur != behaviors.end() ; ++ cur ) {
	    (*cur) -> statement() -> elaborate_sig(des, scope);
      }


      return true;
}


/*
 * A function definition exists within an elaborated module. This
 * matters when elaborating signals, as the ports of the function are
 * created as signals/variables for each instance of the
 * function. That is why PFunction has an elaborate_sig method.
 */
void PFunction::elaborate_sig(Design*des, NetScope*scope) const
{
      if (scope->elab_stage() > 1)
            return;

      scope->set_elab_stage(2);

      perm_string fname = scope->basename();
      assert(scope->type() == NetScope::FUNC);

      elaborate_sig_wires_(des, scope);

	/* Make sure the function has at least one input port. If it
	   fails this test, print an error message. Keep going so we
	   can find more errors. */
      if (ports_ == 0 && !gn_system_verilog()) {
	    cerr << get_fileline() << ": error: Function " << fname
		 << " has no ports." << endl;
	    cerr << get_fileline() << ":      : Functions must have"
		 << " at least one input port." << endl;
	    des->errors += 1;
      }

      NetNet*ret_sig = 0;

	/* Create the signals/variables of the return value and write
	   them into the function scope. */
      switch (return_type_.type) {

	  case PTF_REG:
	  case PTF_REG_S:
	    if (return_type_.range) {
		  ivl_assert(*this, return_type_.range->size() == 1);
		  pform_range_t&return_range = return_type_.range->front();

		  NetExpr*me = elab_and_eval(des, scope,
					     return_range.first, -1,
                                             true);
		  assert(me);
		  NetExpr*le = elab_and_eval(des, scope,
					     return_range.second, -1,
                                             true);
		  assert(le);

		  long mnum = 0, lnum = 0;
		  if ( ! get_const_argument(me, mnum) ) {
			cerr << me->get_fileline() << ": error: "
			      "Unable to evaluate constant expression "
			     << *me << "." << endl;
			des->errors += 1;
		  }

		  if ( ! get_const_argument(le, lnum) ) {
			cerr << le->get_fileline() << ": error: "
			      "Unable to evaluate constant expression "
			     << *le << "." << endl;
			des->errors += 1;
		  }

		  list<netrange_t> packed;
		  packed.push_back(netrange_t(mnum, lnum));
		  ret_sig = new NetNet(scope, fname, NetNet::REG, packed);
		  ret_sig->set_scalar(false);

	    } else {
		  ret_sig = new NetNet(scope, fname, NetNet::REG);
		  ret_sig->set_scalar(true);
	    }
	    ret_sig->set_line(*this);
	    ret_sig->set_signed(return_type_.type == PTF_REG_S);
	    ret_sig->port_type(NetNet::POUTPUT);
	    ret_sig->data_type(IVL_VT_LOGIC);
	    break;

	  case PTF_INTEGER:
	    ret_sig = new NetNet(scope, fname, NetNet::REG, integer_width);
	    ret_sig->set_line(*this);
	    ret_sig->set_signed(true);
	    ret_sig->set_isint(true);
	    ret_sig->set_scalar(false);
	    ret_sig->port_type(NetNet::POUTPUT);
	    ret_sig->data_type(IVL_VT_LOGIC);
	    break;

	  case PTF_TIME:
	    ret_sig = new NetNet(scope, fname, NetNet::REG, 64);
	    ret_sig->set_line(*this);
	    ret_sig->set_signed(false);
	    ret_sig->set_isint(false);
	    ret_sig->set_scalar(false);
	    ret_sig->port_type(NetNet::POUTPUT);
	    ret_sig->data_type(IVL_VT_LOGIC);
	    break;

	  case PTF_REAL:
	  case PTF_REALTIME:
	    ret_sig = new NetNet(scope, fname, NetNet::REG, 1);
	    ret_sig->set_line(*this);
	    ret_sig->set_signed(true);
	    ret_sig->set_isint(false);
	    ret_sig->set_scalar(true);
	    ret_sig->port_type(NetNet::POUTPUT);
	    ret_sig->data_type(IVL_VT_REAL);
	    break;

	  case PTF_ATOM2:
	  case PTF_ATOM2_S:
	    long use_wid;
	    {
		  ivl_assert(*this, return_type_.range->size() == 1);
		  pform_range_t&return_range = return_type_.range->front();
		  NetExpr*me = elab_and_eval(des, scope,
					     return_range.first, -1,
                                             true);
		  ivl_assert(*this, me);
		  NetExpr*le = elab_and_eval(des, scope,
					     return_range.second, -1,
                                             true);
		  ivl_assert(*this, le);

		  long mnum = 0, lnum = 0;
		  if ( ! get_const_argument(me, mnum) ) {
			cerr << me->get_fileline() << ": error: "
			      "Unable to evaluate constant expression "
			     << *me << "." << endl;
			des->errors += 1;
		  }

		  if ( ! get_const_argument(le, lnum) ) {
			cerr << le->get_fileline() << ": error: "
			      "Unable to evaluate constant expression "
			     << *le << "." << endl;
			des->errors += 1;
		  }

		  use_wid = mnum - lnum + 1;
	    }
	    ret_sig = new NetNet(scope, fname, NetNet::REG, use_wid);
	    ret_sig->set_line(*this);
	    ret_sig->set_signed(return_type_.type == PTF_ATOM2_S? true : false);
	    ret_sig->set_isint(true);
	    ret_sig->set_scalar(false);
	    ret_sig->port_type(NetNet::POUTPUT);
	    ret_sig->data_type(IVL_VT_BOOL);
	    break;

	  case PTF_STRING:
	    cerr << get_fileline() << ": sorry: String functions are not supported yet" << endl;
	    break;

	  case PTF_VOID:
	      // Void functions have no return value, so there is no
	      // signal to create here.
	    break;

	  default:
	    if (ports_) {
		  cerr << get_fileline() << ": internal error: I don't know "
		       << "how to deal with return type of function "
		       << scope->basename() << "." << endl;
	    } else {
		    /* If we do not have any ports or a return type this
		     * is probably a bad function definition. */
		  cerr << get_fileline() << ": error: Bad definition for "
		       << "function " << scope->basename() << "?" << endl;
		  return;
	    }
      }

      vector<NetNet*>ports (ports_? ports_->count() : 0);

      if (ports_)
	    for (unsigned idx = 0 ;  idx < ports_->count() ;  idx += 1) {

		    /* Parse the port name into the task name and the reg
		       name. We know by design that the port name is given
		       as two components: <func>.<port>. */

		  perm_string pname = (*ports_)[idx]->basename();

		  NetNet*tmp = scope->find_signal(pname);
		  ports[idx] = 0;

		  if (tmp == 0) {
			cerr << get_fileline() << ": internal error: function "
			     << scope_path(scope) << " is missing port "
			     << pname << "." << endl;
			scope->dump(cerr);
			cerr << get_fileline() << ": Continuing..." << endl;
			des->errors += 1;
			continue;
		  }

		  if (tmp->port_type() == NetNet::NOT_A_PORT) {
			cerr << get_fileline() << ": internal error: function "
			     << scope_path(scope) << " port " << pname
			     << " is a port but is not a port?" << endl;
			des->errors += 1;
			scope->dump(cerr);
			continue;
		  }

		  ports[idx] = tmp;
		  if (tmp->port_type() != NetNet::PINPUT) {
			cerr << tmp->get_fileline() << ": error: function "
			     << scope_path(scope) << " port " << pname
			     << " is not an input port." << endl;
			cerr << tmp->get_fileline() << ":      : Function arguments "
			     << "must be input ports." << endl;
			des->errors += 1;
		  }
	    }


      NetFuncDef*def = 0;
      if (ret_sig)  def = new NetFuncDef(scope, ret_sig, ports);

      assert(def);
      if (debug_elaborate)
	    cerr << get_fileline() << ": debug: "
		 << "Attach function definition to scope "
		 << scope_path(scope) << "." << endl;

      scope->set_func_def(def);

	// Look for further signals in the sub-statement
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

/*
 * A task definition is a scope within an elaborated module. When we
 * are elaborating signals, the scopes have already been created, as
 * have the reg objects that are the parameters of this task. The
 * elaborate_sig method of PTask is therefore left to connect the
 * signals to the ports of the NetTaskDef definition. We know for
 * certain that signals exist (They are in my scope!) so the port
 * binding is sure to work.
 */
void PTask::elaborate_sig(Design*des, NetScope*scope) const
{
      assert(scope->type() == NetScope::TASK);

      elaborate_sig_wires_(des, scope);

      svector<NetNet*>ports (ports_? ports_->count() : 0);
      for (unsigned idx = 0 ;  idx < ports.count() ;  idx += 1) {

	    perm_string port_name = (*ports_)[idx]->basename();

	      /* Find the signal for the port. We know by definition
		 that it is in the scope of the task, so look only in
		 the scope. */
	    NetNet*tmp = scope->find_signal(port_name);

	    if (tmp == 0) {
		  cerr << get_fileline() << ": internal error: "
		       << "Could not find port " << port_name
		       << " in scope " << scope_path(scope) << endl;
		  scope->dump(cerr);
		  des->errors += 1;
	    }

	    ports[idx] = tmp;
      }

      NetTaskDef*def = new NetTaskDef(scope, ports);
      scope->set_task_def(def);

	// Look for further signals in the sub-statement
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

void PBlock::elaborate_sig(Design*des, NetScope*scope) const
{
      NetScope*my_scope = scope;

      if (pscope_name() != 0) {
	    hname_t use_name (pscope_name());
	    my_scope = scope->child(use_name);
	    if (my_scope == 0) {
		  cerr << get_fileline() << ": internal error: "
		       << "Unable to find child scope " << pscope_name()
		       << " in this context?" << endl;
		  des->errors += 1;
		  my_scope = scope;
	    } else {
		  if (debug_elaborate)
			cerr << get_fileline() << ": debug: "
			     << "elaborate_sig descending into "
			     << scope_path(my_scope) << "." << endl;

		  elaborate_sig_wires_(des, my_scope);
	    }
      }

	// elaborate_sig in the statements included in the
	// block. There may be named blocks in there.
      for (unsigned idx = 0 ;  idx < list_.size() ;  idx += 1)
	    list_[idx] -> elaborate_sig(des, my_scope);
}

void PCase::elaborate_sig(Design*des, NetScope*scope) const
{
      if (items_ == 0)
	    return;

      for (unsigned idx = 0 ; idx < items_->count() ; idx += 1) {
	    if ( (*items_)[idx]->stat )
		  (*items_)[idx]->stat ->elaborate_sig(des,scope);
      }
}

void PCondit::elaborate_sig(Design*des, NetScope*scope) const
{
      if (if_)
	    if_->elaborate_sig(des, scope);
      if (else_)
	    else_->elaborate_sig(des, scope);
}

void PDelayStatement::elaborate_sig(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

void PEventStatement::elaborate_sig(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

void PForever::elaborate_sig(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

void PForStatement::elaborate_sig(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

void PRepeat::elaborate_sig(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

void PWhile::elaborate_sig(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_->elaborate_sig(des, scope);
}

static bool evaluate_ranges(Design*des, NetScope*scope,
			    list<netrange_t>&llist,
			    const list<pform_range_t>&rlist)
{
      bool bad_msb = false, bad_lsb = false;

      for (list<pform_range_t>::const_iterator cur = rlist.begin()
		 ; cur != rlist.end() ; ++cur) {
	    long use_msb, use_lsb;

	    NetExpr*texpr = elab_and_eval(des, scope, cur->first, -1, true);
	    if (! eval_as_long(use_msb, texpr)) {
		  cerr << cur->first->get_fileline() << ": error: "
			"Range expressions must be constant." << endl;
		  cerr << cur->first->get_fileline() << "       : "
			"This MSB expression violates the rule: "
		       << *cur->first << endl;
		  des->errors += 1;
		  bad_msb = true;
	    }

	    delete texpr;

	    texpr = elab_and_eval(des, scope, cur->second, -1, true);
	    if (! eval_as_long(use_lsb, texpr)) {
		  cerr << cur->second->get_fileline() << ": error: "
			"Range expressions must be constant." << endl;
		  cerr << cur->second->get_fileline() << "       : "
			"This LSB expression violates the rule: "
		       << *cur->second << endl;
		  des->errors += 1;
		  bad_lsb = true;
	    }

	    delete texpr;

	    llist.push_back(netrange_t(use_msb, use_lsb));
      }

      return bad_msb | bad_lsb;
}

static netstruct_t* elaborate_struct_type(Design*des, NetScope*scope,
					  struct_type_t*struct_type)
{
      netstruct_t*res = new netstruct_t;

      res->packed(struct_type->packed_flag);

      for (list<struct_member_t*>::iterator cur = struct_type->members->begin()
		 ; cur != struct_type->members->end() ; ++ cur) {

	    list<netrange_t>packed_dimensions;

	    struct_member_t*curp = *cur;
	    if (curp->range.get() && ! curp->range->empty()) {
		  bool bad_range;
		  bad_range = evaluate_ranges(des, scope, packed_dimensions, *curp->range);
		  ivl_assert(*curp, !bad_range);
	    } else {
		  packed_dimensions.push_back(netrange_t(0,0));
	    }

	    for (list<decl_assignment_t*>::iterator name = curp->names->begin()
		       ; name != curp->names->end() ;  ++ name) {
		  decl_assignment_t*namep = *name;

		  netstruct_t::member_t memb;
		  memb.name = namep->name;
		  memb.type = curp->type;
		  memb.packed_dims = packed_dimensions;
		  res->append_member(memb);
	    }
      }

      return res;
}

static netparray_t* elaborate_parray_type(Design*des, NetScope*scope,
					  parray_type_t*data_type)
{

      list<netrange_t>packed_dimensions;
      bool bad_range = evaluate_ranges(des, scope, packed_dimensions, * data_type->packed_dims);
      ivl_assert(*data_type, !bad_range);

      netparray_t*res = new netparray_t(packed_dimensions);
	//res->set_line(*data_type);

      return res;
}

static nettype_base_t*elaborate_type(Design*des, NetScope*scope,
				     data_type_t*pform_type)
{
      if (struct_type_t*struct_type = dynamic_cast<struct_type_t*>(pform_type)) {
	    netstruct_t*use_type = elaborate_struct_type(des, scope, struct_type);
	    return use_type;
      }

      cerr << pform_type->get_fileline() << ": sorry: I don't know how to elaborate "
	   << typeid(*pform_type).name() << " here." << endl;
      des->errors += 1;

      return 0;
}

bool test_ranges_eeq(const list<netrange_t>&lef, const list<netrange_t>&rig)
{
      if (lef.size() != rig.size())
	    return false;

      list<netrange_t>::const_iterator lcur = lef.begin();
      list<netrange_t>::const_iterator rcur = rig.begin();
      while (lcur != lef.end()) {
	    if (lcur->get_msb() != rcur->get_msb())
		  return false;
	    if (lcur->get_lsb() != rcur->get_lsb())
		  return false;

	    ++ lcur;
	    ++ rcur;
      }

      return true;
}

/*
 * Elaborate a source wire. The "wire" is the declaration of wires,
 * registers, ports and memories. The parser has already merged the
 * multiple properties of a wire (i.e., "input wire"), so come the
 * elaboration this creates an object in the design that represents the
 * defined item.
 */
NetNet* PWire::elaborate_sig(Design*des, NetScope*scope) const
{
      NetNet::Type wtype = type_;
      bool is_implicit_scalar = false;
      if (wtype == NetNet::IMPLICIT) {
	    wtype = NetNet::WIRE;
	    is_implicit_scalar = true;
      }
      if (wtype == NetNet::IMPLICIT_REG) {
	    wtype = NetNet::REG;
	    is_implicit_scalar = true;
      }

      unsigned wid = 1;
      list<netrange_t>packed_dimensions;

      des->errors += error_cnt_;

	// A signal can not have the same name as a scope object.
      const NetScope *child = scope->child(hname_t(name_));
      if (child) {
	    cerr << get_fileline() << ": error: signal and ";
	    child->print_type(cerr);
	    cerr << " in '" << scope->fullname()
	         << "' have the same name '" << name_ << "'." << endl;
	    des->errors += 1;
      }
	// A signal can not have the same name as a genvar.
      const LineInfo *genvar = scope->find_genvar(name_);
      if (genvar) {
	    cerr << get_fileline() << ": error: signal and genvar in '"
	         << scope->fullname() << "' have the same name '" << name_
	         << "'." << endl;
	    des->errors += 1;
      }
	// A signal can not have the same name as a parameter.
      const NetExpr *ex_msb, *ex_lsb;
      const NetExpr *parm = scope->get_parameter(des, name_, ex_msb, ex_lsb);
      if (parm) {
	    cerr << get_fileline() << ": error: signal and parameter in '"
	         << scope->fullname() << "' have the same name '" << name_
	         << "'." << endl;
	    des->errors += 1;
      }
	// A signal can not have the same name as a named event.
      const NetEvent *event = scope->find_event(name_);
      if (event) {
	    cerr << get_fileline() << ": error: signal and named event in '"
	         << scope->fullname() << "' have the same name '" << name_
	         << "'." << endl;
	    des->errors += 1;
      }

      if (port_set_ || net_set_) {
	    bool bad_range = false;
	    list<netrange_t> plist, nlist;
	    /* If they exist get the port definition MSB and LSB */
	    if (port_set_ && !port_.empty()) {
		  bad_range |= evaluate_ranges(des, scope, plist, port_);
		  nlist = plist;
		    /* An implicit port can have a range so note that here. */
		  is_implicit_scalar = false;
	    }
            assert(port_set_ || port_.empty());

	    /* If they exist get the net/etc. definition MSB and LSB */
	    if (net_set_ && !net_.empty() && !bad_range) {
		  nlist.clear();
		  bad_range |= evaluate_ranges(des, scope, nlist, net_);
	    }
            assert(net_set_ || net_.empty());

	    /* If we find errors here, then give up on this signal. */
	    if (bad_range)
		  return 0;

	    /* We have a port size error */
            if (port_set_ && net_set_ && !test_ranges_eeq(plist, nlist)) {

		  /* Scalar port with a vector net/etc. definition */
		  if (port_.empty()) {
			if (!gn_io_range_error_flag) {
			      cerr << get_fileline()
			           << ": warning: Scalar port ``" << name_
			           << "'' has a vectored net declaration "
				   << nlist << "." << endl;
			} else {
			      cerr << get_fileline()
			           << ": error: Scalar port ``" << name_
			           << "'' has a vectored net declaration "
				   << nlist << "." << endl;
			      des->errors += 1;
			      return 0;
			}
		  }

		  /* Vectored port with a scalar net/etc. definition */
		  if (net_.empty()) {
			cerr << port_.front().first->get_fileline()
			     << ": error: Vectored port ``"
			     << name_ << "'' " << plist
			     << " has a scalar net declaration at "
			     << get_fileline() << "." << endl;
			des->errors += 1;
			return 0;
		  }

		  /* Both vectored, but they have different ranges. */
		  if (!port_.empty() && !net_.empty()) {
			cerr << port_.front().first->get_fileline()
			     << ": error: Vectored port ``"
			     << name_ << "'' " << plist
			     << " has a net declaration " << nlist
			     << " at " << net_.front().first->get_fileline()
			     << " that does not match." << endl;
			des->errors += 1;
			return 0;
		  }
            }

	    packed_dimensions = nlist;
	    wid = netrange_width(packed_dimensions);

      }

      unsigned nattrib = 0;
      attrib_list_t*attrib_list = evaluate_attributes(attributes, nattrib,
						      des, scope);


      list<netrange_t>unpacked_dimensions;
      netdarray_t*netarray = 0;

      for (list<pform_range_t>::const_iterator cur = unpacked_.begin()
		 ; cur != unpacked_.end() ; ++cur) {
	    PExpr*use_lidx = cur->first;
	    PExpr*use_ridx = cur->second;

	      // Special case: If we encounter an undefined
	      // dimensions, then turn this into a dynamic array and
	      // put all the packed dimensions there.
	    if (use_lidx==0 && use_ridx==0) {
		  ivl_assert(*this, netarray==0);
		  netarray = new netdarray_t(packed_dimensions, data_type_, wid);
		  packed_dimensions.clear();
		  continue;
	    }

	      // Cannot handle dynamic arrays of arrays yet.
	    ivl_assert(*this, netarray==0);
	    ivl_assert(*this, use_lidx && use_ridx);

	    NetExpr*lexp = elab_and_eval(des, scope, use_lidx, -1, true);
	    NetExpr*rexp = elab_and_eval(des, scope, use_ridx, -1, true);

	    if ((lexp == 0) || (rexp == 0)) {
		  cerr << get_fileline() << ": internal error: There is "
		       << "a problem evaluating indices for ``"
		       << name_ << "''." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    bool const_flag = true;
	    verinum lval, rval;
	    const_flag &= get_const_argument(lexp, lval);
	    const_flag &= get_const_argument(rexp, rval);
	    delete rexp;
	    delete lexp;

	    long index_l, index_r;
	    if (! const_flag) {
		  cerr << get_fileline() << ": error: The indices "
		       << "are not constant for array ``"
		       << name_ << "''." << endl;
		  des->errors += 1;
                    /* Attempt to recover from error, */
	          index_l = 0;
	          index_r = 0;
	    } else {
		  index_l = lval.as_long();
		  index_r = rval.as_long();
	    }

	    unpacked_dimensions.push_back(netrange_t(index_l, index_r));
      }

      if (data_type_ == IVL_VT_REAL && !packed_dimensions.empty()) {
	    cerr << get_fileline() << ": error: real ";
	    if (wtype == NetNet::REG) cerr << "variable";
	    else cerr << "net";
	    cerr << " '" << name_
	         << "' cannot be declared as a vector, found a range "
		 << packed_dimensions << "." << endl;
	    des->errors += 1;
	    return 0;
      }

	/* If the net type is supply0 or supply1, replace it
	   with a simple wire with a pulldown/pullup with supply
	   strength. In other words, transform:

	   supply0 foo;

	   to:

	   wire foo;
	   pulldown #(supply0) (foo);

	   This reduces the backend burden, and behaves exactly
	   the same. */

      NetLogic*pull = 0;
      if (wtype == NetNet::SUPPLY0 || wtype == NetNet::SUPPLY1) {
	    NetLogic::TYPE pull_type = (wtype==NetNet::SUPPLY1)
		  ? NetLogic::PULLUP
		  : NetLogic::PULLDOWN;
	    pull = new NetLogic(scope, scope->local_symbol(),
				1, pull_type, wid);
	    pull->set_line(*this);
	    pull->pin(0).drive0(IVL_DR_SUPPLY);
	    pull->pin(0).drive1(IVL_DR_SUPPLY);
	    des->add_node(pull);
	    wtype = NetNet::WIRE;

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: "
		       << "Generate a SUPPLY pull for the ";
		  if (wtype == NetNet::SUPPLY0) cerr << "supply0";
		  else cerr << "supply1";
		  cerr << " net." << endl;
	    }
      }


      NetNet*sig = 0;

	// If this is a struct type, then build the net with the
	// struct type.
      if (struct_type_t*struct_type = dynamic_cast<struct_type_t*>(set_data_type_)) {
	    netstruct_t*use_type = elaborate_struct_type(des, scope, struct_type);
	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Create signal " << wtype;
		  if (use_type->packed())
			cerr << " " << use_type->packed_width() << " bit packed struct ";
		  else
			cerr << " struct <> ";
		  cerr << name_;
		  cerr << " in scope " << scope_path(scope) << endl;
	    }

	    sig = new NetNet(scope, name_, wtype, use_type);

      } else if (enum_type_t*enum_type = dynamic_cast<enum_type_t*>(set_data_type_)) {
	    list<named_pexpr_t>::const_iterator sample_name = enum_type->names->begin();
	    netenum_t*use_enum = scope->enumeration_for_name(sample_name->name);

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Create signal " << wtype
		       << " enumeration "
		       << name_ << " in scope " << scope_path(scope)
		       << " with packed_dimensions=" << packed_dimensions
		       << " and packed_width=" << use_enum->packed_width() << endl;
	    }

	    sig = new NetNet(scope, name_, wtype, packed_dimensions, unpacked_dimensions, use_enum);


      } else if (netarray) {

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Create signal " << wtype
		       << " dynamic array "
		       << name_ << " in scope " << scope_path(scope) << endl;
	    }

	    ivl_assert(*this, packed_dimensions.empty());
	    ivl_assert(*this, unpacked_dimensions.empty());
	    sig = new NetNet(scope, name_, wtype, netarray);

      } else if (parray_type_t*parray_type = dynamic_cast<parray_type_t*>(set_data_type_)) {
	      // The pform gives us a parray_type_t for packed arrays
	      // that show up in type definitions. This can be handled
	      // a lot like packed dimensions from other means.

	      // The trick here is that the parray type has an
	      // arbitrary sub-type, and not just a scalar bit...
	    netparray_t*use_type = elaborate_parray_type(des, scope, parray_type);
	      // Should not be getting packed dimensions other then
	      // through the parray type declaration.
	    ivl_assert(*this, packed_dimensions.empty());

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Create signal " << wtype
		       << " parray=" << use_type->packed_dimensions()
		       << " " << name_ << unpacked_dimensions
		       << " in scope " << scope_path(scope) << endl;
	    }

	    nettype_base_t*base_type = elaborate_type(des, scope, parray_type->base_type);
#if 0
	    cerr << get_fileline() << ": sorry: Packed array of "
		 << typeid(*parray_type->base_type).name()
		 << " not supported." << endl;
	    des->errors += 1;
#endif
	    sig = new NetNet(scope, name_, wtype, use_type->packed_dimensions(), unpacked_dimensions, base_type);


      } else {
	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Create signal " << wtype;
		  if (!get_scalar()) {
			cerr << " " << packed_dimensions;
		  }
		  cerr << " " << name_ << unpacked_dimensions;
		  cerr << " in scope " << scope_path(scope) << endl;
	    }

	    sig = new NetNet(scope, name_, wtype, packed_dimensions, unpacked_dimensions);
      }

      if (wtype == NetNet::WIRE) sig->devirtualize_pins();

      ivl_variable_type_t use_data_type = data_type_;
      if (use_data_type == IVL_VT_NO_TYPE) {
	    use_data_type = IVL_VT_LOGIC;
	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: "
		       << "Signal " << name_
		       << " in scope " << scope_path(scope)
		       << " defaults to data type " << use_data_type << endl;
	    }
      }

      sig->data_type(use_data_type);
      sig->set_line(*this);
      sig->port_type(port_type_);
      sig->set_signed(get_signed());
      sig->set_isint(get_isint());
      if (is_implicit_scalar) sig->set_scalar(true);
      else sig->set_scalar(get_scalar());

      if (ivl_discipline_t dis = get_discipline()) {
	    sig->set_discipline(dis);
      }

      if (pull)
	    connect(sig->pin(0), pull->pin(0));

      for (unsigned idx = 0 ;  idx < nattrib ;  idx += 1)
	    sig->attribute(attrib_list[idx].key, attrib_list[idx].val);

      return sig;
}
