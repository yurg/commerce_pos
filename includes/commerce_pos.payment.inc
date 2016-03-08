<?php

/**
 * Payment functionality for POS returns and sales.
 */

/**
 * Callback for the POS sale payment form.
 */
function commerce_pos_sale_payment($form, &$form_state) {
  commerce_pos_payment($form, $form_state, CommercePosService::TRANSACTION_TYPE_SALE);
  return $form;
}

/**
 * Callback for the POS return payment form.
 */
function commerce_pos_return_payment($form, &$form_state) {
  commerce_pos_payment($form, $form_state, CommercePosService::TRANSACTION_TYPE_RETURN);
  return $form;
}

/**
 * Callback to build a POS payment form.
 */
function commerce_pos_payment(&$form, &$form_state, $transaction_type) {
  global $user;

  form_load_include($form_state, 'inc', 'commerce_pos', 'includes/commerce_pos.common');
  commerce_pos_payment_ajax_check($form, $form_state);

  $return = $transaction_type == CommercePosService::TRANSACTION_TYPE_RETURN;
  $payment_methods = commerce_pos_get_payment_methods();
  $wrapper_id = 'commerce-pos-pay-form-wrapper';
  $form['#theme'] = 'commerce_pos_payment';
  $form['#redirect_key'] = $return ? 'return' : 'sale';
  $form['#transaction_type'] = $transaction_type;

  $form['#prefix'] = '<div id="' . $wrapper_id . '" class="' . ($return ? 'return' : 'sale') . '">';
  $form['#suffix'] = '</div>';
  $form_state['wrapper_id'] = $wrapper_id;

  $form_ajax = array(
    'wrapper' => 'commerce-pos-pay-form-wrapper',
    'callback' => 'commerce_pos_payment_wrapper_js',
  );

  if (empty($payment_methods)) {
    drupal_set_message(t('No payment methods have been configured for the POS!'), 'error');
  }

  if (!isset($form_state['transaction'])) {
    if ($transaction = CommercePosService::getCurrentTransaction($transaction_type, $user->uid)) {
      $order_wrapper = $transaction->getOrderWrapper();

      if ($order_wrapper && $order_wrapper->status->value() == 'commerce_pos_in_progress' ) {
        $form_state['transaction'] = $transaction;
        $form_state['order_wrapper'] = $order_wrapper;
      }
    }
  }

  $form['header'] = array(
    '#markup' => theme('commerce_pos_header', array('account' => $user)),
  );

  if (isset($form_state['transaction'])) {
    $order_value = $form_state['order_wrapper']->value();
    $order_balance = commerce_payment_order_balance($order_value);
    $line_item_count = 0;

    foreach ($form_state['order_wrapper']->commerce_line_items as $line_item_wrapper) {
      if (in_array($line_item_wrapper->type->value(), commerce_product_line_item_types())) {
        $line_item_count++;
      }
    }

    $items_link = l(format_plural($line_item_count, '1 Item', '@count Items'), '', array(
      'fragment' => ' ',
      'external' => TRUE,
      'attributes' => array(
        'class' => array('commerce-pos-summary-toggle'),
      ),
    ));

    $replacements = array(
      '!items' => $items_link,
      '@order_id' => $form_state['order_wrapper']->order_id->value(),
    );

    if ($return) {
      $summary_text = t('<div class="order-text">Return - !items - Order #@order_id </div>', $replacements);
    }
    else {
      $summary_text = t('<div class="order-text">Payment - !items - Order #@order_id </div>', $replacements);
    }

    $form['summary'] = array(
      '#markup' => $summary_text,
    );

    $form['summary_table'] = array(
      '#theme' => 'commerce_pos_product_summary',
      '#order' => $form_state['order_wrapper']->value(),
      '#attributes' => array(
        'class' => array('element-invisible')
      ),
    );

    $form['edit_order'] = array(
      '#markup' => l($return ? t('Edit Return') : t('Edit Order'), 'pos/' . $form['#redirect_key'], array(
        'attributes' => array(
          'class' => array('commerce-pos-sale-pay-edit-order'),
        ),
      )),
    );

    $form['payment_options'] = array(
      '#type' => 'container',
      '#tree' => TRUE,
    );

    $payment_ajax = array(
      'wrapper' => 'commerce-pos-sale-keypad-wrapper',
      'callback' => 'commerce_pos_payment_keypad_js',
      'effect' => 'fade',
    );

    foreach ($payment_methods as $payment_method) {
      $form['payment_options'][$payment_method['id']] = array(
        '#type' => 'button',
        '#value' => $payment_method['title'],
        '#name' => 'commerce-pos-payment-method-' . $payment_method['id'],
        '#ajax' => $payment_ajax,
        '#payment_method_id' => $payment_method['id'],
        '#disabled' => $order_balance['amount'] <= 0,
        '#limit_validation_errors' => array(),
      );
    }

    $form['keypad'] = array(
      '#type' => 'container',
      '#id' => 'commerce-pos-sale-keypad-wrapper',
      '#tree' => TRUE,
      '#theme' => 'commerce_pos_keypad',
    );

    if (!empty($form_state['triggering_element']['#payment_method_id'])) {
      $method_id = $form_state['triggering_element']['#payment_method_id'];

      $form['keypad']['amount'] = array(
        '#type' => 'textfield',
        '#title' => t('Enter @title Amount', array(
          '@title' => $payment_methods[$method_id]['title'],
        )),
        '#required' => TRUE,
        '#attributes' => array(
            'autofocus' => 'autofocus',
            'autocomplete' => 'off',
            'class' => array(
              'commerce-pos-payment-keypad-amount'
            )
        ),
      );

      $form['keypad']['amount']['#attached']['js'][] = array(
        'type' => 'setting',
        'data' => array(
          'commercePosPayment' => array(
            'focusInput' => TRUE,
            'selector' => '.commerce-pos-payment-keypad-amount',
          ),
        ),
      );

      $form['keypad']['add'] = array(
        '#type' => 'submit',
        '#value' => t('Add'),
        '#name' => 'commerce-pos-pay-keypad-add',
        '#validate' => array('commerce_pos_payment_add_payment_validate'),
        '#submit' => array('commerce_pos_payment_add_payment_submit'),
        '#ajax' => $form_ajax,
        '#payment_method_id' => $method_id,
        '#element_key' => 'add-payment'
      );
    }

    $form['balance'] = array(
      '#type' => 'container',
      '#id' => 'commerce-pos-pay-balance-wrapper',
    );

    $order_total = commerce_pos_price_order_format($form_state['order_wrapper'], 'commerce_order_total', $return);

    $form['balance']['order_total'] = array(
      '#markup' => render($order_total),
    );

    $order_value = $form_state['order_wrapper']->value();
    $form['balance']['summary'] = array(
      '#markup' => commerce_pos_balance_summary($order_value),
    );

    $form['balance']['actions'] = array(
      '#type' => 'actions',
    );

    $form['balance']['actions']['remove_payment'] = array(
      '#type' => 'submit',
      '#value' => t('Remove Payment'),
      '#attributes' => array(
        'class' => array(
          'commerce-pos-transaction-btn',
          'commerce-pos-remove-payment'
        ),
        'style' => array('display: none')
      ),
      '#submit' => array('commerce_pos_payment_remove_payment_submit'),
      '#ajax' => array_merge($form_ajax, array('event' => 'remove_payment')),
      '#limit_validation_errors' => array(
        array('remove_payment_text')
      ),
      '#element_key' => 'remove-payment',
    );

    $form['balance']['remove_payment_text'] = array(
      '#type' => 'hidden',
      '#attributes' => array(
        'class' => array(
          'commerce-pos-remove-payment-input'
        ),
        'style' => array('display: none')
      ),
    );

    $form['balance']['actions']['finish'] = array(
      '#type' => 'submit',
      '#value' => t('Finish'),
      '#disabled' => $order_balance['amount'] > 0,
      '#validate' => array('commerce_pos_payment_validate'),
      '#submit' => array('commerce_pos_payment_finish'),
      '#attributes' => array(
        'class' => array('commerce-pos-payment-finish-btn')
      )
    );

    $form['balance']['actions']['park'] = array(
      '#type' => 'button',
      '#value' => t('Park'),
      '#attributes' => array(
        'class' => array('commerce-pos-transaction-btn'),
      ),
      '#ajax' => $form_ajax,
      '#limit_validation_errors' => array(),
      '#element_key' => 'park-transaction',
    );

    $form['balance']['actions']['void'] = array(
      '#type' => 'button',
      '#value' => t('Void'),
      '#attributes' => array(
        'class' => array('commerce-pos-transaction-btn'),
      ),
      '#ajax' => $form_ajax,
      '#limit_validation_errors' => array(),
      '#element_key' => 'void-transaction',
    );

    $form['balance']['actions']['customer'] = array(
      '#type' => 'textfield',
      '#title' => t('Customer'),
      '#autocomplete_path' => 'pos/user/autocomplete',
      '#ajax' => array(
        'wrapper' => 'commerce-pos-customer-input-wrapper',
        'callback' => 'commerce_pos_customer_js',
      ),
      '#prefix' => '<div id="commerce-pos-customer-input-wrapper">',
      '#suffix' => '</div>',
      '#element_key' => 'customer-update',
      '#attributes' => array(
        'placeholder' => t('email@address.com'),
      ),
      '#default_value' => !empty($transaction->data['customer email']) ? $transaction->data['customer email'] : NULL,
    );
  }
  else {
    $form['no_transactions'] = array(
      '#markup' => t('You do not currently have any active transactions. Either retrieve a parked transaction or start a new one.'),
    );
  }

  $form['parked_transactions'] = commerce_pos_parked_transactions_section($transaction_type, $form_ajax, !empty($form_state['transaction']));

  $form['#attached']['css'][] = drupal_get_path('module', 'commerce_pos') . '/css/commerce_pos.css';
  $form['#attached']['css'][] = drupal_get_path('module', 'commerce_pos') . '/css/style.css';
  $form['#attached']['js'][] = drupal_get_path('module', 'commerce_pos') . '/js/commerce_pos.payment.js';
}

/**
 * AJAX callback for the Pay form keypad.
 */
function commerce_pos_payment_keypad_js($form, &$form_state) {
  return $form['keypad'];
}

/**
 * AJAX callback for the Pay form's wrapper.
 */
function commerce_pos_payment_wrapper_js($form, &$form_state) {
  return $form;
}

/**
 * Validation handler for the "Add" payment button.
 */
function commerce_pos_payment_add_payment_validate($form, &$form_state) {
  if (!is_numeric($form_state['values']['keypad']['amount'])) {
    form_set_error('keypad][amount', t('Payment amount must be a number.'));
  }
}

/**
 * Submit handler for the "Add" payment button.
 */
function commerce_pos_payment_add_payment_submit($form, &$form_state) {
  $payment_method = commerce_payment_method_load($form_state['triggering_element']['#payment_method_id']);
  $order_wrapper = $form_state['transaction']->getOrderWrapper();
  $order_id = $order_wrapper->order_id->value();
  $transaction = commerce_payment_transaction_new($form_state['triggering_element']['#payment_method_id'], $order_id);
  $transaction->instance_id = $payment_method['method_id'] . '|commerce_pos';
  $transaction->amount = $form_state['values']['keypad']['amount'] * 100;
  $transaction->currency_code = $order_wrapper->commerce_order_total->currency_code->value();
  $transaction->status = COMMERCE_PAYMENT_STATUS_SUCCESS;
  $transaction->message = '';
  commerce_payment_transaction_save($transaction);

  $form_state['input']['keypad']['amount'] = NULL;
  $form_state['triggering_element']['#payment_method_id'] = NULL;

  $form_state['rebuild'] = TRUE;
}

/**
 * Validation handler for the pay form's "Finish" button.
 */
function commerce_pos_payment_validate($form, &$form_state) {

}

function commerce_pos_payment_remove_payment_submit($form, &$form_state){
  $transaction_id = $form_state['values']['remove_payment_text'];

  commerce_payment_transaction_delete($transaction_id);

  $form_state['rebuild'] = TRUE;
}

/**
 * Submit handler for the pay form's "Finish" button.
 */
function commerce_pos_payment_finish($form, &$form_state) {
  $form_state['transaction']->doAction('completeTransaction');
  $form_state['redirect'] = 'pos/' . $form['#redirect_key'];
  drupal_set_message(t('Transaction Completed'));
}

/**
 * Helper function to check for AJAX submissions on the POS pay form.
 *
 * This will look at what triggered the AJAX submission and act accordingly.
 */
function commerce_pos_payment_ajax_check($form, &$form_state) {
  if (isset($form_state['triggering_element'])) {
    $triggering_element = $form_state['triggering_element'];

    if (!empty($form_state['triggering_element']['#element_key'])) {
      /* @var CommercePosTransaction $transaction */
      $transaction = $form_state['transaction'];
      $form_state['order_updated'] = TRUE;

      switch ($triggering_element['#element_key']) {
        case 'retrieve-parked-transaction':
          $parked_transaction = CommercePosService::loadTransaction($triggering_element['#transaction_id']);
          $parked_transaction->unpark();

          if ($transaction) {
            $transaction->void();
          }

          unset($form_state['transaction']);

          drupal_set_message(t('Transaction @id retrieved.', array('@id' => $parked_transaction->transactionId)));
          break;
        case 'park-transaction':
          $transaction->doAction('park');
          unset($form_state['transaction']);
          drupal_set_message(t('Transaction @id parked.', array('@id' => $transaction->transactionId)));
          break;

        case 'void-transaction':
          $transaction->void();
          unset($form_state['transaction']);
          drupal_set_message(t('Transaction @id voided.', array('@id' => $transaction->transactionId)));
          break;

        case 'customer-update':
          $transaction->data['customer email'] = !empty($triggering_element['#value']) ? $triggering_element['#value'] : NULL;
          $transaction->doAction('save');
          break;

        default:
          // If we didn't reach any of the above, it means we don't need to reload the order.
          $form_state['order_updated'] = FALSE;
      }
    }
  }
}